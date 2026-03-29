#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace weixin::media {

enum class MediaType {
  IMAGE,
  VIDEO,
  VOICE,
  FILE
};

// Detect MIME type from file content (magic numbers)
std::string DetectMimeTypeFromContent(const std::vector<uint8_t>& data);

// Detect MIME type from file extension
std::string DetectMimeTypeFromExtension(const std::string& path);

// Get media type category from MIME type
MediaType GetMediaTypeFromMime(const std::string& mime);

// Get file extension from MIME type
std::string GetExtensionFromMime(const std::string& mime);

} // namespace weixin::media
