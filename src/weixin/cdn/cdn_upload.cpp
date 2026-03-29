#include "cdn/cdn_upload.hpp"
#include "cdn/aes_ecb.hpp"

#include <httplib.h>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdint>
#include <vector>
#include <string>

namespace weixin::cdn {

UploadResult UploadToCdn(
    const std::string& upload_url,
    const std::vector<uint8_t>& data,
    const std::string& content_type,
    int max_retries) {
  
  UploadResult result;
  
  for (int attempt = 0; attempt < max_retries; ++attempt) {
    httplib::Client client(upload_url);
    client.set_connection_timeout(30);
    client.set_read_timeout(60);
    
    httplib::Headers headers = {
      {"Content-Type", content_type}
    };
    
    auto res = client.Put(
        "/",
        headers,
        reinterpret_cast<const char*>(data.data()),
        data.size(),
        content_type
    );
    
    if (res && res->status == 200) {
      result.success = true;
      return result;
    }
    
    // Don't retry on 4xx errors
    if (res && res->status >= 400 && res->status < 500) {
      result.error_msg = "Client error: " + std::to_string(res->status);
      return result;
    }
    
    // Exponential backoff
    if (attempt < max_retries - 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100 * (1 << attempt)));
    }
  }
  
  result.error_msg = "Upload failed after " + std::to_string(max_retries) + " retries";
  return result;
}

UploadResult UploadFileToCdn(
    const std::string& upload_url,
    const std::string& file_path,
    int max_retries) {
  
  // Read file
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    UploadResult result;
    result.error_msg = "Cannot open file: " + file_path;
    return result;
  }
  
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
  
  return UploadToCdn(upload_url, data, "application/octet-stream", max_retries);
}

} // namespace weixin::cdn
