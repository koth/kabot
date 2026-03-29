#pragma once

#include <optional>
#include <string>

namespace weixin::auth {

// Account configuration
struct WeixinAccount {
  std::string account_id;
  std::string token;
  std::optional<std::string> name;
  bool enabled = true;
  std::string base_url = "https://ilinkai.weixin.qq.com";
  std::string cdn_base_url = "https://novac2c.cdn.weixin.qq.com/c2c";
  std::optional<int> route_tag;
};

// Normalize account ID (replace special characters)
std::string NormalizeAccountId(const std::string& account_id);

} // namespace weixin::auth
