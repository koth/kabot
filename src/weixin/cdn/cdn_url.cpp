#include "cdn/cdn_url.hpp"
#include "cdn/aes_ecb.hpp"

#include <openssl/md5.h>
#include <openssl/evp.h>
#include <iomanip>
#include <sstream>
#include <vector>

namespace weixin::cdn {

std::string BuildCdnUrl(const std::string& base_url,
                        const std::string& encrypt_query_param) {
  if (encrypt_query_param.empty()) {
    return base_url;
  }
  
  std::string url = base_url;
  if (url.find('?') == std::string::npos) {
    url += "?";
  } else {
    url += "&";
  }
  url += encrypt_query_param;
  
  return url;
}

std::vector<uint8_t> ParseAesKey(const std::string& key_str) {
  std::vector<uint8_t> key;
  
  // Try to decode as base64 first
  // TODO: Implement base64 decoding
  // For now, assume it's raw hex string
  
  if (key_str.length() == 32) {
    // Hex string
    for (size_t i = 0; i < 32; i += 2) {
      std::string byte_str = key_str.substr(i, 2);
      uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
      key.push_back(byte);
    }
  } else if (key_str.length() == 16) {
    // Raw key
    key.assign(key_str.begin(), key_str.end());
  }
  
  return key;
}

} // namespace weixin::cdn
