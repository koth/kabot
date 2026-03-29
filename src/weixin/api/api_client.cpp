#include "api/api_client.hpp"
#include "util/random.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>

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

APIResponse<std::vector<WeixinMessage>> APIClient::GetUpdates(
    const std::optional<std::string>& buffer,
    int timeout_seconds) {
  std::string path = kBasePath;
  path += "/get_updates?timeout=" + std::to_string(timeout_seconds);
  if (buffer.has_value()) {
    path += "&buf=" + buffer.value();
  }
  
  auto res = http_client_->Get(path.c_str());

  if (!res) {
    APIResponse<std::vector<WeixinMessage>> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Network error"};
    return error_result;
  }

  if (res->status != 200) {
    APIResponse<std::vector<WeixinMessage>> error_result;
    error_result.success = false;
    error_result.error = APIError{static_cast<int>(res->status), "HTTP error: " + std::to_string(res->status)};
    return error_result;
  }
  
  try {
    auto j = nlohmann::json::parse(res->body);
    
    if (j.contains("errcode") && j["errcode"].get<int>() != 0) {
      APIError error{
        j["errcode"].get<int>(),
        j.value("errmsg", "Unknown error")
      };
      APIResponse<std::vector<WeixinMessage>> result;
      result.success = false;
      result.error = error;
      return result;
    }
    
    std::vector<WeixinMessage> messages;
    if (j.contains("message_list") && j["message_list"].is_array()) {
      for (const auto& msg_json : j["message_list"]) {
        WeixinMessage msg;
        if (msg_json.contains("message_id")) {
          msg.message_id = msg_json["message_id"].get<uint64_t>();
        }
        if (msg_json.contains("from_user_id")) {
          msg.from_user_id = msg_json["from_user_id"].get<std::string>();
        }
        if (msg_json.contains("context_token")) {
          msg.context_token = msg_json["context_token"].get<std::string>();
        }
        messages.push_back(msg);
      }
    }

    APIResponse<std::vector<WeixinMessage>> success_result;
    success_result.success = true;
    success_result.data = messages;
    return success_result;
  } catch (const std::exception& e) {
    APIResponse<std::vector<WeixinMessage>> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, std::string("Parse error: ") + e.what()};
    return error_result;
  }
}

APIResponse<void> APIClient::SendTextMessage(const std::string& user_id,
                                              const std::string& context_token,
                                              const std::string& content) {
  nlohmann::json body;
  body["to_user_id"] = user_id;
  body["context_token"] = context_token;
  body["item_list"] = nlohmann::json::array();
  nlohmann::json item;
  item["item_type"] = static_cast<int>(MessageItemType::TEXT);
  item["content"] = content;
  body["item_list"].push_back(item);
  
  auto res = http_client_->Post(
      (std::string(kBasePath) + "/send_message").c_str(),
      body.dump(),
      "application/json"
  );

  if (!res || res->status != 200) {
    APIResponse<void> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Failed to send message"};
    return error_result;
  }

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
  auto res = http_client_->Get((std::string(kBasePath) + "/get_qrcode").c_str());

  if (!res) {
    APIResponse<QRCodeData> error_result;
    error_result.success = false;
    error_result.error = APIError{-1, "Network error"};
    return error_result;
  }

  if (res->status != 200) {
    APIResponse<QRCodeData> error_result;
    error_result.success = false;
    error_result.error = APIError{static_cast<int>(res->status), "HTTP error: " + std::to_string(res->status)};
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
    data.qrcode_url = j.value("qrcode_url", "");
    data.qrcode_token = j.value("qrcode_token", "");
    data.expired_timestamp = j.value("expired_timestamp", static_cast<uint64_t>(0));
    
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
    const std::string& qrcode_token) {
  nlohmann::json body;
  body["qrcode_token"] = qrcode_token;
  
  auto res = http_client_->Post(
      (std::string(kBasePath) + "/get_qrcode_status").c_str(),
      body.dump(),
      "application/json"
  );

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
    
    // Parse status string to enum
    std::string status_str = j.value("status", "");
    if (status_str == "PENDING") {
      status_response.status = QRCodeStatus::PENDING;
    } else if (status_str == "SCANNED") {
      status_response.status = QRCodeStatus::SCANNED;
    } else if (status_str == "CONFIRMED") {
      status_response.status = QRCodeStatus::CONFIRMED;
    } else if (status_str == "EXPIRED") {
      status_response.status = QRCodeStatus::EXPIRED;
    } else if (status_str == "REDIRECT") {
      status_response.status = QRCodeStatus::REDIRECT;
    } else {
      status_response.status = QRCodeStatus::PENDING;  // Default
    }
    
    // Parse optional fields
    if (j.contains("bot_info") && !j["bot_info"].is_null()) {
      status_response.bot_info = j["bot_info"].get<std::string>();
    }
    if (j.contains("redirect_url") && !j["redirect_url"].is_null()) {
      status_response.redirect_url = j["redirect_url"].get<std::string>();
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
