#include "auth/login_qr.hpp"
#include "api/api_client.hpp"
#include "auth/account.hpp"
#include "auth/accounts.hpp"

#include <iostream>
#include <thread>
#include <chrono>

namespace weixin::auth {

std::string QRLogin::StartLogin(const std::string& base_url) {
  base_url_ = base_url;
  cancelled_ = false;
  retry_count_ = 0;
  
  // Create a temporary account without token for QR code generation
  WeixinAccount temp_account;
  temp_account.base_url = base_url_;
  temp_account.token = "";  // No token yet
  
  // Create API client with temporary account
  api::APIClient client(temp_account, "kabot", "1.0.0");
  
  // Get QR code
  auto result = client.GetQRCode();
  if (!result.success || !result.data.has_value()) {
    std::cerr << "[weixin] Failed to get QR code: " 
              << (result.error.has_value() ? result.error->errmsg : "Unknown error")
              << std::endl;
    return "";
  }
  
  const auto& qr_data = result.data.value();
  qrcode_ = qr_data.qrcode;
  
  // Display QR code URL
  std::cout << "\n========================================" << std::endl;
  std::cout << "WeChat QR Code Login" << std::endl;
  std::cout << "========================================" << std::endl;
  std::cout << "Please scan the QR code with WeChat:" << std::endl;
  std::cout << qr_data.qrcode_url << std::endl;
  std::cout << "========================================\n" << std::endl;
  
  return qr_data.qrcode_url;
}

std::optional<std::string> QRLogin::PollStatus(int max_retries) {
  if (qrcode_.empty()) {
    std::cerr << "[weixin] QR code not initialized. Call StartLogin first." << std::endl;
    return std::nullopt;
  }
  
  // Create temporary account for polling
  WeixinAccount temp_account;
  temp_account.base_url = base_url_;
  temp_account.token = "";
  
  api::APIClient client(temp_account, "kabot", "1.0.0");
  
  int poll_count = 0;
  constexpr int max_polls = 180;  // 6 minutes max (2 seconds per poll)
  
  while (!cancelled_ && poll_count < max_polls) {
    auto result = client.GetQRCodeStatus(qrcode_);
    
    if (!result.success) {
      std::cerr << "[weixin] Failed to get QR status: "
                << (result.error.has_value() ? result.error->errmsg : "Unknown error")
                << std::endl;
      retry_count_++;
      if (retry_count_ >= max_retries) {
        std::cerr << "[weixin] Max retries exceeded" << std::endl;
        return std::nullopt;
      }
      std::this_thread::sleep_for(std::chrono::seconds(2));
      continue;
    }
    
    const auto& status_response = result.data.value();
    
    switch (status_response.status) {
      case api::QRCodeStatus::PENDING:
        // Still waiting for user to scan
        break;
        
      case api::QRCodeStatus::SCANNED:
        std::cout << "[weixin] QR code scanned, waiting for confirmation..." << std::endl;
        break;
        
      case api::QRCodeStatus::CONFIRMED:
        std::cout << "[weixin] Login confirmed!" << std::endl;
        // Extract token from bot_info if available
        if (status_response.bot_info.has_value()) {
          // Parse bot_info JSON to extract token
          try {
            nlohmann::json bot_info = nlohmann::json::parse(status_response.bot_info.value());
            if (bot_info.contains("token")) {
              std::string token = bot_info["token"].get<std::string>();
              std::cout << "[weixin] Token obtained successfully" << std::endl;
              return token;
            }
          } catch (const std::exception& e) {
            std::cerr << "[weixin] Failed to parse bot_info: " << e.what() << std::endl;
          }
        }
        std::cerr << "[weixin] Login confirmed but no token in response" << std::endl;
        return std::nullopt;
        
      case api::QRCodeStatus::EXPIRED:
        std::cerr << "[weixin] QR code expired. Please restart login." << std::endl;
        return std::nullopt;
        
      case api::QRCodeStatus::REDIRECT:
        std::cerr << "[weixin] Redirect required: "
                  << (status_response.redirect_url.has_value() 
                      ? status_response.redirect_url.value() 
                      : "Unknown URL")
                  << std::endl;
        return std::nullopt;
    }
    
    poll_count++;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }
  
  if (cancelled_) {
    std::cout << "[weixin] Login cancelled by user" << std::endl;
  } else {
    std::cerr << "[weixin] QR code polling timeout" << std::endl;
  }
  
  return std::nullopt;
}

void QRLogin::Cancel() {
  cancelled_ = true;
}

// Standalone function for easy QR login
bool PerformQRLogin(const std::string& account_id, const std::string& base_url) {
  QRLogin qr_login;
  
  std::string qr_url = qr_login.StartLogin(base_url);
  if (qr_url.empty()) {
    std::cerr << "[weixin] Failed to start QR login" << std::endl;
    return false;
  }
  
  auto token = qr_login.PollStatus(3);
  if (!token.has_value()) {
    std::cerr << "[weixin] QR login failed" << std::endl;
    return false;
  }
  
  // Save account with token
  WeixinAccount account;
  account.account_id = account_id;
  account.token = token.value();
  account.base_url = base_url;
  account.enabled = true;
  
  if (Accounts::Save(account)) {
    std::cout << "[weixin] Account saved successfully: " << account_id << std::endl;
    return true;
  } else {
    std::cerr << "[weixin] Failed to save account" << std::endl;
    return false;
  }
}

} // namespace weixin::auth
