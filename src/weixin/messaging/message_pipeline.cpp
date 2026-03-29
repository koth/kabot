#include "messaging/message_pipeline.hpp"
#include "messaging/slash_commands.hpp"
#include "storage/context_token_store.hpp"

#include <filesystem>
#include <chrono>

namespace weixin::messaging {

MessagePipeline::MessagePipeline(const std::string& account_id,
                                 const std::string& media_output_dir)
    : account_id_(account_id)
    , media_output_dir_(media_output_dir) {}

void MessagePipeline::Process(const api::WeixinMessage& msg) {
  // Log if in debug mode
  if (debug_mode_) {
    LogMessage(msg);
  }
  
  // Process the message
  auto processed = processor_.Process(msg, media_output_dir_);
  if (!processed.has_value()) {
    return;
  }
  
  auto& data = processed.value();
  
  // Save context token
  if (!data.context_token.empty()) {
    storage::ContextTokenStore store(account_id_);
    store.Save(data.user_id, data.context_token);
  }
  
  // Handle slash commands
  if (!data.text.empty() && data.text[0] == '/') {
    HandleSlashCommand(data);
    return;
  }
  
  // Call handler
  if (handler_) {
    handler_(data);
  }
}

void MessagePipeline::SetHandler(MessageHandler handler) {
  handler_ = handler;
}

void MessagePipeline::SetDebugMode(bool enabled) {
  debug_mode_ = enabled;
}

void MessagePipeline::HandleSlashCommand(
    const InboundProcessor::ProcessedMessage& msg) {
  auto result = SlashCommands::Handle(msg.text, msg.user_id);
  
  if (result.handled) {
    // Command was handled, send response
    if (handler_ && !result.response.empty()) {
      InboundProcessor::ProcessedMessage response = msg;
      response.text = result.response;
      handler_(response);
    }
    
    // Handle debug toggle
    if (result.toggle_debug) {
      SetDebugMode(!debug_mode_);
    }
  } else {
    // Not a recognized command, treat as normal message
    if (handler_) {
      handler_(msg);
    }
  }
}

void MessagePipeline::LogMessage(const api::WeixinMessage& msg) {
  // TODO: Implement proper logging
  // For now, just a placeholder
  (void)msg;
}

} // namespace weixin::messaging
