#include "api/api_client.hpp"
#include "util/random.hpp"
#include "util/redact.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include <iostream>

// Debug logging macros - will be disabled later
// #define WEIXIN_LOG_DEBUG(msg) std::cout << "[weixin:debug] " << msg << std::endl
#define WEIXIN_LOG_DEBUG(msg)

// #define WEIXIN_LOG_INFO(msg) std::cout << "[weixin:info] " << msg << std::endl
#define WEIXIN_LOG_INFO(msg)
#define WEIXIN_LOG_WARN(msg) std::cout << "[weixin:warn] " << msg << std::endl
#define WEIXIN_LOG_ERROR(msg) std::cerr << "[weixin:error] " << msg << std::endl

namespace weixin::api {

namespace {

std::string Uint32ToBase64(uint32_t value) {
  static const char* chars = 
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(8);
  
  for (int i = 0; i < 4; ++i) {
    uint8_t byte = (value >> (i * 8)) & 0xFF;
    result += chars[(byte >> 2) & 0x3F];
    result += chars[((byte << 4) & 0x30)];
  }
  result += "==";
  return result;
}

std::string VersionToUint32String(const std::string& version) {
  // Parse version like "1.2.3" to uint32 format
  std::stringstream ss(version);
  int major = 0, minor = 0, patch = 0;
  char dot;
  ss >> major >> dot >> minor >> dot >> patch;
  
  uint32_t version_num = (major << 16) | (minor << 8) | patch;
  
  std::stringstream result;
  result << version_num;
  return result.str();
}

} // anonymous namespace

APIClient::APIClient(const auth::WeixinAccount& account,
                     const std::string& app_id,
                     const std::string& app_version)
    : account_(account), app_id_(app_id), app_version_(app_version) {
  http_client_ = std::make_unique<httplib::Client>(account_.base_url);
  http_client_->set_connection_timeout(30);
  http_client_->set_read_timeout(30);
  SetupHeaders();
}

APIClient::~APIClient() = default;

void APIClient::SetupHeaders() {
  http_client_->set_default_headers({
    {"iLink-App-Id", app_id_},
    {"iLink-App-ClientVersion", VersionToUint32String(app_version_)},
    {"AuthorizationType", "ilink_bot_token"},
    {"Authorization", BuildAuthHeader()},
    {"X-WECHAT-UIN", Uint32ToBase64(util::GenerateRandomUint32())}
  });
  
  if (account_.route_tag.has_value()) {
    http_client_->set_default_headers({
      {"SKRouteTag", std::to_string(account_.route_tag.value())}
    });
  }
}

std::string APIClient::BuildAuthHeader() const {
  return "Bearer " + account_.token;
}

std::string APIClient::GenerateWechatUin() const {
  return Uint32ToBase64(util::GenerateRandomUint32());
}

APIResponse<GetUpdatesData> APIClient::GetUpdates(
    const std::optional<std::string>& buffer,
    int timeout_seconds) {
  WEIXIN_LOG_DEBUG("GetUpdates: Starting poll, buffer length=" << buffer.value_or("").length());
  
  // Use POST with JSON body to match TypeScript implementation
  nlohmann::json body;
  body["get_updates_buf"] = buffer.value_or("");  // Always include, empty string if none

  // Add base_info with channel_version (required by API)
  nlohmann::json base_info;
  base_info["channel_version"] = app_version_;
  body["base_info"] = base_info;

  std::string body_str = body.dump();
  WEIXIN_LOG_DEBUG("GetUpdates: Request body=" << body_str);

  // Set HTTP timeout (timeout_seconds is for HTTP client, not sent in body)
  http_client_->set_read_timeout(timeout_seconds);

  WEIXIN_LOG_DEBUG("GetUpdates: Sending POST to " << kBasePath << "/getupdates");
  auto res = http_client_->Post(
      (std::string(kBasePath) + "/getupdates").c_str(),
      body_str,
      "application/json"
  );

  if (!res) {
    WEIXIN_LOG_ERROR("GetUpdates: Network error - no response");
    APIResponse<GetUpdatesData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Network error"};
    return error_result;
  }
  
  WEIXIN_LOG_DEBUG("GetUpdates: Response status=" << res->status << ", body length=" << res->body.length());

  if (res->status != 200) {
    APIResponse<GetUpdatesData> error_result;
    error_result.success = false;
    error_result.error = APIError{static_cast<int>(res->status), "HTTP error: " + std::to_string(res->status) + " body: " + res->body};
    return error_result;
  }

  try {
    // Check for empty response body
    if (res->body.empty()) {
      WEIXIN_LOG_ERROR("GetUpdates: Empty response body");
      APIResponse<GetUpdatesData> error_result;
      error_result.success = false;
      error_result.error = APIError{-1, "Empty response body from server"};
      return error_result;
    }
    
    auto j = nlohmann::json::parse(res->body);
    WEIXIN_LOG_DEBUG("GetUpdates: Parsed JSON response");
    
    // Check if response is null or not an object
    if (j.is_null() || !j.is_object()) {
      WEIXIN_LOG_ERROR("GetUpdates: Invalid JSON response - not an object");
      APIResponse<GetUpdatesData> error_result;
      error_result.success = false;
      error_result.error = APIError{-1, "Invalid JSON response format"};
      return error_result;
    }

    // Check for API error (using "ret" field as in TypeScript)
    if (j.contains("ret") && j["ret"].get<int>() != 0) {
      WEIXIN_LOG_ERROR("GetUpdates: API error ret=" << j["ret"].get<int>() << ", errmsg=" << j.value("errmsg", "unknown"));
      APIError error{
        j["ret"].get<int>(),
        j.value("errmsg", "Unknown error")
      };
      APIResponse<GetUpdatesData> result;
      result.success = false;
      result.error = error;
      return result;
    }

    GetUpdatesData data;

    // Parse messages from "msgs" array (TypeScript uses "msgs" not "message_list")
    if (j.contains("msgs") && j["msgs"].is_array()) {
      size_t msg_count = j["msgs"].size();
      WEIXIN_LOG_INFO("GetUpdates: Received " << msg_count << " messages");
      
      for (const auto& msg_json : j["msgs"]) {
        WeixinMessage msg;
        if (msg_json.contains("message_id")) {
          msg.message_id = msg_json["message_id"].get<uint64_t>();
        }
        if (msg_json.contains("from_user_id")) {
          msg.from_user_id = msg_json["from_user_id"].get<std::string>();
          WEIXIN_LOG_DEBUG("GetUpdates: Found from_user_id=" << msg.from_user_id.value());
        } else {
          WEIXIN_LOG_WARN("GetUpdates: Message missing from_user_id!");
        }
        if (msg_json.contains("to_user_id")) {
          msg.to_user_id = msg_json["to_user_id"].get<std::string>();
        }
        if (msg_json.contains("context_token")) {
          msg.context_token = msg_json["context_token"].get<std::string>();
        }
        // Parse timestamp fields
        if (msg_json.contains("create_time_ms")) {
          msg.create_time_ms = msg_json["create_time_ms"].get<uint64_t>();
        }
        if (msg_json.contains("update_time_ms")) {
          msg.update_time_ms = msg_json["update_time_ms"].get<uint64_t>();
        }
        if (msg_json.contains("delete_time_ms")) {
          msg.delete_time_ms = msg_json["delete_time_ms"].get<uint64_t>();
        }
        // Parse item_list for content (TypeScript uses "item_list", not "msg_item_list")
        if (msg_json.contains("item_list") && msg_json["item_list"].is_array()) {
          WEIXIN_LOG_DEBUG("GetUpdates: Found item_list with " << msg_json["item_list"].size() << " items");
          for (const auto& item_json : msg_json["item_list"]) {
            MessageItem item;
            if (item_json.contains("type")) {
              int type_val = item_json["type"].get<int>();
              item.type = static_cast<MessageItemType>(type_val);
            }
            // Parse text item
            if (item_json.contains("text_item") && !item_json["text_item"].is_null()) {
              TextItem text;
              if (item_json["text_item"].contains("text")) {
                text.text = item_json["text_item"]["text"].get<std::string>();
              }
              item.text_item = text;
            }
            msg.item_list.push_back(item);
          }
        }
        data.messages.push_back(msg);
      }
    }

    // Extract get_updates_buf for next poll (CRITICAL!)
    if (j.contains("get_updates_buf") && !j["get_updates_buf"].is_null()) {
      data.next_buffer = j["get_updates_buf"].get<std::string>();
      WEIXIN_LOG_DEBUG("GetUpdates: Extracted buffer, length=" << data.next_buffer.length());
    } else {
      WEIXIN_LOG_WARN("GetUpdates: No get_updates_buf in response!");
    }

    WEIXIN_LOG_INFO("GetUpdates: Success, processed " << data.messages.size() << " messages");
    APIResponse<GetUpdatesData> success_result;
    success_result.success = true;
    success_result.data = data;
    return success_result;
  } catch (const std::exception& e) {
    APIResponse<GetUpdatesData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, std::string("Parse error: ") + e.what()};
    return error_result;
  }
}

APIResponse<void> APIClient::SendTextMessage(const std::string& user_id,
                                              const std::string& context_token,
                                              const std::string& content) {
  WEIXIN_LOG_INFO("SendTextMessage: Sending to user=" << user_id << ", content_length=" << content.length());
  
  // Build message according to TypeScript structure
  nlohmann::json msg;
  msg["from_user_id"] = "";  // Empty for bot messages
  msg["to_user_id"] = user_id;
  msg["client_id"] = util::GenerateMessageId();
  msg["message_type"] = static_cast<int>(MessageType::BOT);
  msg["message_state"] = static_cast<int>(MessageState::FINISH);
  msg["context_token"] = context_token;
  
  // Build item_list with text item (field name is "item_list" not "msg_item_list")
  nlohmann::json item_list = nlohmann::json::array();
  nlohmann::json text_item;
  text_item["type"] = static_cast<int>(MessageItemType::TEXT);
  text_item["text_item"] = nlohmann::json::object();
  text_item["text_item"]["text"] = content;
  item_list.push_back(text_item);
  msg["item_list"] = item_list;
  
  // Wrap in body with msg key
  nlohmann::json body;
  body["msg"] = msg;
  
  std::string body_str = body.dump();
  WEIXIN_LOG_DEBUG("SendTextMessage: Request body=" << body_str);
  
  auto res = http_client_->Post(
      (std::string(kBasePath) + "/sendmessage").c_str(),
      body_str,
      "application/json"
  );

  if (!res) {
    WEIXIN_LOG_ERROR("SendTextMessage: Network error - no response");
    APIResponse<void> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Network error"};
    return error_result;
  }
  
  WEIXIN_LOG_DEBUG("SendTextMessage: Response status=" << res->status << ", body=" << res->body);

  if (res->status != 200) {
    WEIXIN_LOG_ERROR("SendTextMessage: HTTP error status=" << res->status << ", body=" << res->body);
    APIResponse<void> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Failed to send message, status=" + std::to_string(res->status)};
    return error_result;
  }
  
  WEIXIN_LOG_INFO("SendTextMessage: Message sent successfully");

  APIResponse<void> success_result;
  success_result.success = true;
  return success_result;
}

APIResponse<UploadUrlData> APIClient::GetUploadUrl(UploadMediaType media_type,
                                                     size_t file_size) {
  nlohmann::json body;
  body["media_type"] = static_cast<int>(media_type);
  body["file_size"] = file_size;
  
  auto res = http_client_->Post(
      (std::string(kBasePath) + "/get_cdn_upload_url").c_str(),
      body.dump(),
      "application/json"
  );

  if (!res || res->status != 200) {
    APIResponse<UploadUrlData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Failed to get upload URL"};
    return error_result;
  }

  try {
    auto j = nlohmann::json::parse(res->body);
    UploadUrlData data;
    data.upload_url = j.value("upload_url", "");
    data.aes_key = j.value("aes_key", "");
    data.media_id = j.value("media_id", "");
    APIResponse<UploadUrlData> success_result;
    success_result.success = true;
    success_result.data = data;
    return success_result;
  } catch (...) {
    APIResponse<UploadUrlData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Failed to parse response"};
    return error_result;
  }
}

APIResponse<QRCodeData> APIClient::GetQRCode() {
  // Endpoint: ilink/bot/get_bot_qrcode?bot_type=3
  // Note: bot_type is always "3" for this API, not the app_id
  std::string path = std::string(kBasePath) + "/get_bot_qrcode?bot_type=3";
  auto res = http_client_->Get(path.c_str());

  if (!res) {
    APIResponse<QRCodeData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Network error"};
    return error_result;
  }

  if (res->status != 200) {
    APIResponse<QRCodeData> error_result;
    error_result.success = false;
    error_result.error = APIError{static_cast<int>(res->status), "HTTP error: " + std::to_string(res->status) + " body: " + res->body};
    return error_result;
  }

  try {
    auto j = nlohmann::json::parse(res->body);
    
    if (j.contains("errcode") && j["errcode"].get<int>() != 0) {
      APIError error{
        j["errcode"].get<int>(),
        j.value("errmsg", "Unknown error")
      };
      APIResponse<QRCodeData> result;
      result.success = false;
      result.error = error;
      return result;
    }

    QRCodeData data;
    data.qrcode = j.value("qrcode", "");
    data.qrcode_url = j.value("qrcode_img_content", "");
    
    APIResponse<QRCodeData> success_result;
    success_result.success = true;
    success_result.data = data;
    return success_result;
  } catch (const std::exception& e) {
    APIResponse<QRCodeData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, std::string("Parse error: ") + e.what()};
    return error_result;
  }
}

APIResponse<QRCodeStatusResponse> APIClient::GetQRCodeStatus(
    const std::string& qrcode) {
  // Endpoint: ilink/bot/get_qrcode_status?qrcode=xxx
  std::string path = std::string(kBasePath) + "/get_qrcode_status?qrcode=" + httplib::detail::encode_url(qrcode);
  
  auto res = http_client_->Get(path.c_str());

  if (!res || res->status != 200) {
    APIResponse<QRCodeStatusResponse> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Failed to get QR code status"};
    return error_result;
  }

  try {
    auto j = nlohmann::json::parse(res->body);
    
    if (j.contains("errcode") && j["errcode"].get<int>() != 0) {
      APIError error{
        j["errcode"].get<int>(),
        j.value("errmsg", "Unknown error")
      };
      APIResponse<QRCodeStatusResponse> result;
      result.success = false;
      result.error = error;
      return result;
    }

    QRCodeStatusResponse status_response;
    
    // Parse status string to enum (values from TypeScript reference)
    std::string status_str = j.value("status", "");
    if (status_str == "wait") {
      status_response.status = QRCodeStatus::PENDING;
    } else if (status_str == "scaned") {
      status_response.status = QRCodeStatus::SCANNED;
    } else if (status_str == "confirmed") {
      status_response.status = QRCodeStatus::CONFIRMED;
    } else if (status_str == "expired") {
      status_response.status = QRCodeStatus::EXPIRED;
    } else if (status_str == "scaned_but_redirect") {
      status_response.status = QRCodeStatus::REDIRECT;
    } else {
      status_response.status = QRCodeStatus::PENDING;  // Default
    }
    
    // Parse optional fields (matching TypeScript field names)
    if (j.contains("bot_token") && !j["bot_token"].is_null()) {
      status_response.bot_token = j["bot_token"].get<std::string>();
    }
    if (j.contains("ilink_bot_id") && !j["ilink_bot_id"].is_null()) {
      status_response.ilink_bot_id = j["ilink_bot_id"].get<std::string>();
    }
    if (j.contains("baseurl") && !j["baseurl"].is_null()) {
      status_response.baseurl = j["baseurl"].get<std::string>();
    }
    if (j.contains("ilink_user_id") && !j["ilink_user_id"].is_null()) {
      status_response.ilink_user_id = j["ilink_user_id"].get<std::string>();
    }
    if (j.contains("redirect_host") && !j["redirect_host"].is_null()) {
      status_response.redirect_host = j["redirect_host"].get<std::string>();
    }
    if (j.contains("error_msg") && !j["error_msg"].is_null()) {
      status_response.error_msg = j["error_msg"].get<std::string>();
    }
    
    APIResponse<QRCodeStatusResponse> success_result;
    success_result.success = true;
    success_result.data = status_response;
    return success_result;
  } catch (const std::exception& e) {
    APIResponse<QRCodeStatusResponse> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, std::string("Parse error: ") + e.what()};
    return error_result;
  }
}

} // namespace weixin::api
