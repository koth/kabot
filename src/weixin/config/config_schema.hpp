#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace weixin::config {

// Global configuration
struct WeixinGlobalConfig {
  std::string log_level = "info";
  std::optional<std::string> state_dir;
  int default_timeout_seconds = 30;
};

// Account configuration schema validation
bool ValidateAccountConfig(const std::string& json_str, std::string& error_msg);

} // namespace weixin::config
