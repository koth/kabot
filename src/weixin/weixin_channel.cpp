#include "weixin_channel.hpp"
#include "auth/accounts.hpp"
#include "auth/login_qr.hpp"
#include "storage/sync_buffer.hpp"
#include "storage/context_token_store.hpp"
#include "util/random.hpp"
#include "util/redact.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <regex>
#include <thread>
#include <iostream>

namespace weixin {

WeixinChannel::WeixinChannel(const Config& config, 
                             kabot::bus::MessageBus& bus)
    : ChannelBase(config.name.empty() ? "weixin" : config.name,
                  bus,
                  config.allow_from,
                  config.binding_agent)
    , config_(config) {
  account_.account_id = config_.account_id;
  account_.base_url = config_.base_url;
  account_.cdn_base_url = config_.cdn_base_url;
  account_.route_tag = config_.route_tag;
  // Note: token is loaded from storage in Start(), not from config
}

WeixinChannel::~WeixinChannel() {
  Stop();
}

void WeixinChannel::Start() {
  if (running_) {
    return;
  }
  
  // Load token from storage (obtained via QR code login)
  // If account_id is not set, use name as fallback
  std::string account_id = account_.account_id.empty() ? config_.name : account_.account_id;
  
  auto loaded_account = auth::Accounts::Load(account_id);
  if (loaded_account.has_value()) {
    account_.token = loaded_account->token;
    // Override with stored values if available
    if (!loaded_account->base_url.empty()) {
      account_.base_url = loaded_account->base_url;
    }
    if (!loaded_account->cdn_base_url.empty()) {
      account_.cdn_base_url = loaded_account->cdn_base_url;
    }
  }
  
  if (account_.token.empty()) {
    // No token found - initiate QR code login flow
    std::cout << "[weixin] No token found for account: " << account_id << std::endl;
    std::cout << "[weixin] Initiating QR code login flow..." << std::endl;
    
    if (!auth::PerformQRLogin(account_id, account_.base_url)) {
      std::cerr << "[weixin] QR code login failed for account: " << account_id << std::endl;
      return;
    }
    
    // Reload account after successful QR login
    loaded_account = auth::Accounts::Load(account_id);
    if (loaded_account.has_value() && !loaded_account->token.empty()) {
      account_.token = loaded_account->token;
      std::cout << "[weixin] Token obtained and saved for account: " << account_id << std::endl;
    } else {
      std::cerr << "[weixin] Failed to load token after QR login" << std::endl;
      return;
    }
  }
  
  // Load sync buffer for resume
  storage::SyncBuffer sync_store(account_id);
  auto buffer = sync_store.Load();
  if (buffer.has_value()) {
    sync_buffer_ = buffer.value();
  }
  
  // Initialize API client
  api_client_ = std::make_unique<api::APIClient>(account_, config_.app_id, config_.app_version);
  
  running_ = true;
  polling_ = true;
  
  poll_thread_ = std::make_unique<std::thread>([this]() {
    PollLoop();
  });
}

void WeixinChannel::Stop() {
  running_ = false;
  polling_ = false;
  
  if (poll_thread_ && poll_thread_->joinable()) {
    poll_thread_->join();
  }
  
  poll_thread_.reset();
  api_client_.reset();
}

bool WeixinChannel::Send(const kabot::bus::OutboundMessage& msg) {
  //std::cout << "[weixin:info] Send: Called with chat_id=" << msg.chat_id << ", reply_to=" << msg.reply_to << std::endl;
  
  if (!api_client_) {
    std::cout << "[weixin:error] Send: api_client_ is null" << std::endl;
    return false;
  }
  
  auto it_action = msg.metadata.find("action");
  if (it_action != msg.metadata.end() && it_action->second == "typing") {
    std::cout << "[weixin:debug] Send: Typing indicator, skipping" << std::endl;
    // Typing indicator - Weixin API doesn't support this directly
    return true;
  }
  
  std::string chat_id = msg.chat_id;
  if (chat_id.empty() && !msg.reply_to.empty()) {
    // Try to get chat_id from reply_to
    auto it = chat_id_map_.find(msg.reply_to);
    if (it != chat_id_map_.end()) {
      chat_id = it->second;
     // std::cout << "[weixin:debug] Send: Found chat_id from reply_to=" << chat_id << std::endl;
    }
  }
  
  if (chat_id.empty()) {
    std::cout << "[weixin:error] Send: chat_id is empty and cannot be resolved" << std::endl;
    return false;
  }
  
  //std::cout << "[weixin:info] Send: Sending to chat_id=" << chat_id << ", content_length=" << msg.content.length() << std::endl;
  
  // Get context token
  storage::ContextTokenStore token_store(account_.account_id);
  auto context_token = token_store.Load(chat_id);
  
  // Convert markdown to plain text
  std::string text = ConvertMarkdownToText(msg.content);
  
  //std::cout << "[weixin:debug] Send: context_token=" << (context_token.has_value() ? "present" : "empty") << std::endl;
  
  // Send message
  //std::cout << "[weixin:info] Send: Calling SendTextMessage" << std::endl;
  auto result = api_client_->SendTextMessage(
      chat_id,
      context_token.value_or(""),
      text
  );
  
  if (result.success) {
    //std::cout << "[weixin:info] Send: Message sent successfully" << std::endl;
  } else {
    std::cout << "[weixin:error] Send: Failed to send message";
    if (result.error.has_value()) {
      std::cout << " error=" << result.error.value().errmsg;
    }
    std::cout << std::endl;
  }
  
  return result.success;
}

void WeixinChannel::PollLoop() {
  monitor::ConnectionManager conn_mgr;

  //std::cout << "[weixin:info] PollLoop: Starting message polling loop" << std::endl;
  //std::cout << "[weixin:info] PollLoop: Initial buffer length=" << sync_buffer_.length() << std::endl;

  while (running_ && polling_) {
    // Check if we should pause (session expired, etc.)
    if (conn_mgr.ShouldPause()) {
      int delay_ms = monitor::ConnectionManager::kSessionExpirationPauseSec * 1000;
      std::cout << "[weixin:warn] PollLoop: Session expired, pausing for " << delay_ms << "ms" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      conn_mgr.Reset();
      continue;
    }

    // Check if in cooldown period
    if (conn_mgr.IsInCooldown()) {
      int delay_ms = monitor::ConnectionManager::kCooldownPeriodSec * 1000;
      std::cout << "[weixin:warn] PollLoop: In cooldown, waiting for " << delay_ms << "ms" << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      conn_mgr.Reset();
      continue;
    }

    //std::cout << "[weixin:debug] PollLoop: Calling GetUpdates with buffer length=" << sync_buffer_.length() << std::endl;
    auto result = api_client_->GetUpdates(
        sync_buffer_.empty() ? std::nullopt : std::make_optional(sync_buffer_),
        30  // 30 second timeout
    );

    if (result.success) {
      conn_mgr.RecordSuccess();

      // CRITICAL: Save the buffer for next poll (this is how we track position)
      if (result.data.has_value()) {
        const auto& data = result.data.value();
        //std::cout << "[weixin:info] PollLoop: GetUpdates succeeded, received " << data.messages.size() << " messages" << std::endl;

        // Update sync buffer with new buffer from server
        if (!data.next_buffer.empty()) {
          sync_buffer_ = data.next_buffer;
          //std::cout << "[weixin:debug] PollLoop: Updated buffer, new length=" << sync_buffer_.length() << std::endl;

          // Save to storage for resume
          storage::SyncBuffer sync_store(account_.account_id);
          sync_store.Save(sync_buffer_);
         // std::cout << "[weixin:debug] PollLoop: Saved buffer to storage" << std::endl;
        } else {
          std::cout << "[weixin:warn] PollLoop: Response has empty buffer!" << std::endl;
        }

        // Process messages
        for (size_t i = 0; i < data.messages.size(); ++i) {
         // std::cout << "[weixin:debug] PollLoop: Processing message " << (i + 1) << "/" << data.messages.size() << std::endl;
          ProcessMessage(data.messages[i]);
        }
      } else {
        //std::cout << "[weixin:debug] PollLoop: GetUpdates success but no data" << std::endl;
      }
    } else {
      int delay_ms = 0;
      bool should_retry = conn_mgr.RecordFailure(
          result.error.value_or(api::APIError{}), delay_ms);

      std::cout << "[weixin:error] PollLoop: GetUpdates failed, error=";
      if (result.error.has_value()) {
        std::cout << "[" << result.error.value().errcode << "] " << result.error.value().errmsg;
      } else {
        std::cout << "unknown";
      }
      std::cout << ", should_retry=" << should_retry << ", delay=" << delay_ms << "ms" << std::endl;

      if (!should_retry) {
        if (conn_mgr.ShouldPause()) {
          std::cout << "[weixin:warn] PollLoop: Entering pause state" << std::endl;
          continue;
        }
        if (conn_mgr.IsInCooldown()) {
          std::cout << "[weixin:warn] PollLoop: Entering cooldown" << std::endl;
          continue;
        }
      }

      // Wait before retrying
      if (delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
      }
    }
  }

  std::cout << "[weixin:info] PollLoop: Stopping (running=" << running_ << ", polling=" << polling_ << ")" << std::endl;
}

void WeixinChannel::ProcessMessage(const api::WeixinMessage& msg) {
  //std::cout << "[weixin:debug] ProcessMessage: Started processing message" << std::endl;

  if (!msg.from_user_id.has_value()) {
    std::cout << "[weixin:warn] ProcessMessage: Message has no from_user_id, skipping" << std::endl;
    return;
  }

  std::string user_id = msg.from_user_id.value();
  //std::cout << "[weixin:debug] ProcessMessage: From user=" << user_id << std::endl;

  // Check if user is allowed
  if (!IsAllowed(user_id)) {
    std::cout << "[weixin:info] ProcessMessage: User " << user_id << " is not allowed, skipping" << std::endl;
    return;
  }

  // Save context token
  if (msg.context_token.has_value()) {
    //std::cout << "[weixin:debug] ProcessMessage: Saving context token for user=" << user_id << std::endl;
    storage::ContextTokenStore token_store(account_.account_id);
    token_store.Save(user_id, msg.context_token.value());
    context_tokens_[user_id] = msg.context_token.value();
  } else {
    std::cout << "[weixin:debug] ProcessMessage: Message has no context token" << std::endl;
  }

  // Extract text content
  std::string text;
  //std::cout << "[weixin:debug] ProcessMessage: Processing " << msg.item_list.size() << " message items" << std::endl;
  for (const auto& item : msg.item_list) {
   // std::cout << "[weixin:debug] ProcessMessage: Item type=" << static_cast<int>(item.type) << std::endl;
    if (item.type == api::MessageItemType::TEXT && item.text_item.has_value()) {
      text = item.text_item.value().text;
      //std::cout << "[weixin:info] ProcessMessage: Extracted text message, length=" << text.length() << std::endl;
      break;
    }
  }

  // Create message ID for tracking
  std::string message_id;
  if (msg.message_id.has_value()) {
    message_id = std::to_string(msg.message_id.value());
    chat_id_map_[message_id] = user_id;
    //std::cout << "[weixin:debug] ProcessMessage: Message ID=" << message_id << std::endl;
  } else {
    std::cout << "[weixin:warn] ProcessMessage: Message has no ID" << std::endl;
  }

  // Build metadata
  std::unordered_map<std::string, std::string> metadata;
  metadata["message_id"] = message_id;
  metadata["user_id"] = user_id;

  //std::cout << "[weixin:info] ProcessMessage: Publishing message to bus, user=" << user_id << ", text_length=" << text.length() << std::endl;
  // Publish to message bus
  HandleMessage(user_id, user_id, text, {}, metadata);
  //std::cout << "[weixin:debug] ProcessMessage: Message published successfully" << std::endl;
}

std::string WeixinChannel::ConvertMarkdownToText(const std::string& markdown) {
  std::string result = markdown;
  
  // Bold: **text** or __text__ -> text
  result = std::regex_replace(result, std::regex(R"(\*\*(.+?)\*\*)"), "$1");
  result = std::regex_replace(result, std::regex(R"(__(.+?)__)"), "$1");
  
  // Italic: *text* or _text_ -> text
  result = std::regex_replace(result, std::regex(R"(\*(.+?)\*)"), "$1");
  result = std::regex_replace(result, std::regex(R"(_(.+?)_)"), "$1");
  
  // Code: `text` -> text
  result = std::regex_replace(result, std::regex(R"(`(.+?)`)"), "$1");
  
  // Links: [text](url) -> text (url)
  result = std::regex_replace(result, std::regex(R"(\[([^\]]+)\]\(([^)]+)\))"), "$1 ($2)");
  
  return result;
}

} // namespace weixin
