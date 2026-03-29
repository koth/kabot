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

// CDN media reference
struct CDNMedia {
  std::optional<std::string> encrypt_query_param;
  std::optional<std::string> aes_key;
  std::optional<std::string> full_url;
};

// Message content item (matches TypeScript)
struct TextItem {
  std::string text;
};

struct ImageItem {
  std::string media_id;
  std::optional<CDNMedia> media;
};

struct MessageItem {
  MessageItemType type = MessageItemType::NONE;  // Field name is "type" not "item_type"
  std::optional<TextItem> text_item;
  std::optional<ImageItem> image_item;
  // TODO: Add voice_item, file_item, video_item
};

// Core message structure (matches TypeScript implementation)
struct WeixinMessage {
  std::optional<uint64_t> seq;
  std::optional<uint64_t> message_id;  // Maps to msg_id in JSON
  std::optional<std::string> client_id;
  std::optional<std::string> session_id;
  std::optional<std::string> group_id;
  std::optional<std::string> from_user_id;  // Maps to from_username in JSON
  std::optional<std::string> to_user_id;    // Maps to to_username in JSON
  std::optional<std::string> context_token; // Maps to context in JSON
  std::optional<MessageType> message_type;
  std::optional<MessageState> message_state;
  std::vector<MessageItem> item_list;  // Field name is "item_list" in JSON
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

// GetUpdates response includes messages and buffer for next poll
struct GetUpdatesData {
  std::vector<WeixinMessage> messages;
  std::string next_buffer;  // get_updates_buf for next poll
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
  std::optional<std::string> bot_token;        // Direct token field
  std::optional<std::string> ilink_bot_id;     // Bot ID from response
  std::optional<std::string> baseurl;          // Base URL for API
  std::optional<std::string> ilink_user_id;    // User ID
  std::optional<std::string> redirect_host;    // Redirect host for IDC
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
