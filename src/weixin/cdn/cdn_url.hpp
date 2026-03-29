#pragma once

#include <string>
#include <vector>
#include <optional>

namespace weixin::cdn {

// CDN URL construction
std::string BuildCdnUrl(const std::string& base_url,
                        const std::string& encrypt_query_param);

// Parse AES key from different formats
std::vector<uint8_t> ParseAesKey(const std::string& key_str);

} // namespace weixin::cdn
