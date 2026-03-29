#pragma once

// Prevent Windows macro conflicts
#ifdef SendMessage
#undef SendMessage
#endif

#include "api/api_types.hpp"
#include "auth/account.hpp"

#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace httplib {
class Client;
}

namespace weixin::api {

// HTTP client for Weixin iLink API
class APIClient {
public:
  APIClient(const auth::WeixinAccount& account, 
            const std::string& app_id,
            const std::string& app_version);
  ~APIClient();

  // Long-polling for updates
  APIResponse<GetUpdatesData> GetUpdates(
      const std::optional<std::string>& buffer,
      int timeout_seconds = 30);

  // Send text message
  APIResponse<void> SendTextMessage(const std::string& user_id,
                                      const std::string& context_token,
                                      const std::string& content);

  // Send typing indicator
  APIResponse<void> SendTyping(const std::string& user_id,
                                 const std::string& context_token,
                                 TypingStatus status);

  // Get CDN upload URL
  APIResponse<UploadUrlData> GetUploadUrl(UploadMediaType media_type,
                                           size_t file_size);

  // QR Code login
  APIResponse<QRCodeData> GetQRCode();
  APIResponse<QRCodeStatusResponse> GetQRCodeStatus(
      const std::string& qrcode);

  // Get bot config
  APIResponse<nlohmann::json> GetConfig(const std::string& user_id);

private:
  void SetupHeaders();
  std::string BuildAuthHeader() const;
  std::string GenerateWechatUin() const;
  
  auth::WeixinAccount account_;
  std::string app_id_;
  std::string app_version_;
  std::unique_ptr<httplib::Client> http_client_;
  
  static constexpr const char* kBasePath = "/ilink/bot";
};

} // namespace weixin::api
