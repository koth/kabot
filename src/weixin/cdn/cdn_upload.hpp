#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace weixin::cdn {

// Upload file to CDN with retry logic
struct UploadResult {
  bool success = false;
  std::string error_msg;
  std::string cdn_url;
  std::string media_id;
};

// Upload data to pre-signed CDN URL
UploadResult UploadToCdn(
    const std::string& upload_url,
    const std::vector<uint8_t>& data,
    const std::string& content_type = "application/octet-stream",
    int max_retries = 3);

// Upload file from disk
UploadResult UploadFileToCdn(
    const std::string& upload_url,
    const std::string& file_path,
    int max_retries = 3);

} // namespace weixin::cdn
