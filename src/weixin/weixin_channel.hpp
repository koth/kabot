#pragma once

// Prevent Windows macro conflicts
#ifdef SendMessage
#undef SendMessage
#endif

#include "channels/channel_base.hpp"
#include "auth/account.hpp"
#include "api/api_client.hpp"
#include "monitor/connection_manager.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

namespace weixin {

// Main WeChat channel implementation
class WeixinChannel : public kabot::channels::ChannelBase {
public:
  struct Config {
    std::string name;
    std::string account_id;
    // Note: token is NOT configured here.
    // It is obtained via QR code login and stored in ~/.kabot/
    // WeixinChannel will automatically load it from storage on Start().
    std::vector<std::string> allow_from;
    std::string binding_agent;
    std::string app_id;
    std::string app_version;
    std::string base_url = "https://ilinkai.weixin.qq.com";
    std::string cdn_base_url = "https://novac2c.cdn.weixin.qq.com/c2c";
    std::optional<int> route_tag;
  };

  WeixinChannel(const Config& config, 
                kabot::bus::MessageBus& bus);
  ~WeixinChannel() override;

  // ChannelBase interface
  void Start() override;
  void Stop() override;
  bool Send(const kabot::bus::OutboundMessage& msg) override;
  
  // Prevent Windows macro conflicts
  #ifdef SendMessage
  #undef SendMessage
  #endif

  // QR Code login flow
  bool StartQRLogin(std::string& qrcode_url);
  bool PollQRStatus(std::string& token);

  // Account management
  auth::WeixinAccount& GetAccount() { return account_; }

private:
  void PollLoop();
  void ProcessMessage(const api::WeixinMessage& msg);
  std::string ConvertMarkdownToText(const std::string& markdown);
  
  Config config_;
  auth::WeixinAccount account_;
  std::unique_ptr<api::APIClient> api_client_;
  
  std::unique_ptr<std::thread> poll_thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> polling_{false};
  
  // State
  std::string sync_buffer_;
  std::unordered_map<std::string, std::string> context_tokens_;
  std::unordered_map<std::string, std::string> chat_id_map_;
  
  // QR Login state
  std::string qrcode_token_;
  int qr_retry_count_ = 0;
  static constexpr int kMaxQRRetries = 3;
};

} // namespace weixin
