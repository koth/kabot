#pragma once

#include "api/api_types.hpp"
#include <string>
#include <vector>
#include <optional>

namespace weixin::cdn {

// Upload file with encryption and orchestration
struct FileUploadResult {
  bool success = false;
  std::string error_msg;
  std::string media_id;
  std::string cdn_url;
};

// Upload file to Weixin CDN
// 1. Calculate MD5
// 2. Generate AES key
// 3. Get upload URL from API
// 4. Encrypt and upload
FileUploadResult UploadFile(
    const std::string& file_path,
    api::UploadMediaType media_type);

// Upload data to Weixin CDN
FileUploadResult UploadData(
    const std::vector<uint8_t>& data,
    api::UploadMediaType media_type,
    const std::string& filename);

} // namespace weixin::cdn
