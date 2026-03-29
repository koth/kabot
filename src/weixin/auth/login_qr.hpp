#pragma once

#include "api/api_types.hpp"
#include <string>
#include <functional>
#include <optional>

namespace weixin::auth {

// QR Code login flow
class QRLogin {
public:
  using QRCodeCallback = std::function<void(const std::string& qrcode_url)>;
  using StatusCallback = std::function<void(api::QRCodeStatus status, const std::string& info)>;
  
  // Start QR login flow
  // Returns QR code URL for display
  std::string StartLogin(const std::string& base_url);
  
  // Poll for QR code status
  // Returns token on successful confirmation
  std::optional<std::string> PollStatus(int max_retries = 3);
  
  // Cancel login
  void Cancel();
  
private:
  std::string base_url_;
  std::string qrcode_;
  bool cancelled_ = false;
  int retry_count_ = 0;
};

// Standalone function for easy QR login
// Performs complete QR login flow and saves account
// Returns true on success
bool PerformQRLogin(const std::string& account_id, const std::string& base_url);

} // namespace weixin::auth
