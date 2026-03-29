#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

namespace weixin::api {

// Message types
enum class MessageType : uint8_t {
  NONE = 0,
  USER = 1,
  BOT = 2
};

enum class MessageItemType : uint8_t {
  NONE = 0,
  TEXT = 1,
  IMAGE = 2,
  VOICE = 3,
  FILE = 4,
  VIDEO = 5
};

enum class MessageState : uint8_t {
  NEW = 0,
  GENERATING = 1,
  FINISH = 2
};

enum class TypingStatus : uint8_t {
  TYPING = 1,
  CANCEL = 2
};

enum class UploadMediaType : uint8_t {
  IMAGE = 1,
  VIDEO = 2,
  FILE = 3,
  VOICE = 4
};

// Message content item
struct MessageItem {
  MessageItemType item_type = MessageItemType::NONE;
  std::optional<std::string> item_id;
  std::optional<std::string> content;
};

// CDN media reference
struct CDNMedia {
  std::optional<std::string> encrypt_query_param;
  std::optional<std::string> aes_key;
  std::optional<std::string> full_url;
};

// Core message structure
struct WeixinMessage {
  std::optional<uint64_t> seq;
  std::optional<uint64_t> message_id;
  std::optional<std::string> from_user_id;
  std::optional<std::string> to_user_id;
  std::optional<std::string> context_token;
  std::vector<MessageItem> item_list;
};

// API response structures
struct APIError {
  int errcode = 0;
  std::string errmsg;
  bool IsSessionExpired() const { return errcode == -14; }
};

template<typename T>
struct APIResponse {
  bool success = false;
  std::optional<T> data;
  std::optional<APIError> error;

  bool HasError() const { return error.has_value(); }
  bool IsSessionExpired() const { return error && error->IsSessionExpired(); }
};

// Specialization for void (no data)
template<>
struct APIResponse<void> {
  bool success = false;
  std::optional<APIError> error;

  bool HasError() const { return error.has_value(); }
  bool IsSessionExpired() const { return error && error->IsSessionExpired(); }
};

// QR Code login structures
struct QRCodeData {
  std::string qrcode;        // QR code token for polling
  std::string qrcode_url;    // QR code image URL for display
};

enum class QRCodeStatus {
  PENDING,
  SCANNED,
  CONFIRMED,
  EXPIRED,
  REDIRECT
};

struct QRCodeStatusResponse {
  QRCodeStatus status;
  std::optional<std::string> bot_info;
  std::optional<std::string> redirect_url;
  std::optional<std::string> error_msg;
};

// Upload URL response
struct UploadUrlData {
  std::string upload_url;
  std::string aes_key;
  std::string media_id;
};

// Bot info from login
struct BotInfo {
  std::string token;
  std::optional<std::string> bot_name;
  std::optional<std::string> bot_id;
};

} // namespace weixin::api
