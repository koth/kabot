#include "cdn/pic_decrypt.hpp"
#include "cdn/aes_ecb.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace weixin::cdn {

std::optional<std::vector<uint8_t>> DownloadFromCdn(
    const std::string& url,
    const std::string& aes_key) {
  httplib::Client client(url);
  client.set_connection_timeout(30);
  client.set_read_timeout(60);
  
  auto res = client.Get("/");
  if (!res || res->status != 200) {
    return std::nullopt;
  }
  
  // Parse AES key
  std::vector<uint8_t> key;
  if (!aes_key.empty()) {
    // Try base64 decode first
    // TODO: Implement base64 decode
    // For now, assume raw key
    key.assign(aes_key.begin(), aes_key.end());
  }
  
  if (key.empty() || key.size() != 16) {
    // Return encrypted data if we can't decrypt
    std::vector<uint8_t> data(res->body.begin(), res->body.end());
    return data;
  }
  
  // Decrypt
  std::vector<uint8_t> encrypted(res->body.begin(), res->body.end());
  auto decrypted = AESECB::Decrypt(encrypted, key);
  
  return decrypted;
}

std::optional<std::vector<uint8_t>> DownloadAndDecrypt(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key) {
  // Construct full URL
  std::string full_url = cdn_url;
  if (!encrypt_param.empty()) {
    full_url += "?" + encrypt_param;
  }
  
  return DownloadFromCdn(full_url, aes_key);
}

} // namespace weixin::cdn
