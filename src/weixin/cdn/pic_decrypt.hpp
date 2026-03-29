#pragma once

#include <string>
#include <vector>
#include <optional>

namespace weixin::cdn {

// Download file from CDN URL
std::optional<std::vector<uint8_t>> DownloadFromCdn(
    const std::string& url,
    const std::string& aes_key);

// Download and decrypt media
std::optional<std::vector<uint8_t>> DownloadAndDecrypt(
    const std::string& cdn_url,
    const std::string& encrypt_param,
    const std::string& aes_key);

} // namespace weixin::cdn
