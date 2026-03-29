#include "media/mime_detector.hpp"

#include <fstream>
#include <map>

namespace weixin::media {

namespace {

// Magic numbers for common file types
const std::map<std::string, std::vector<uint8_t>> kMagicNumbers = {
    // Images
    {"image/png", {0x89, 0x50, 0x4E, 0x47}},
    {"image/jpeg", {0xFF, 0xD8, 0xFF}},
    {"image/gif", {0x47, 0x49, 0x46, 0x38}},
    {"image/bmp", {0x42, 0x4D}},
    {"image/webp", {0x52, 0x49, 0x46, 0x46}},
    
    // Audio
    {"audio/mpeg", {0x49, 0x44, 0x33}},  // MP3
    {"audio/wav", {0x52, 0x49, 0x46, 0x46}},
    {"audio/ogg", {0x4F, 0x67, 0x67, 0x53}},
    
    // Video
    {"video/mp4", {0x00, 0x00, 0x00, 0x18, 0x66, 0x74, 0x79, 0x70}},
    {"video/avi", {0x52, 0x49, 0x46, 0x46}},
    
    // Documents
    {"application/pdf", {0x25, 0x50, 0x44, 0x46}},
    {"application/zip", {0x50, 0x4B, 0x03, 0x04}},
};

// Extension to MIME type mapping
const std::map<std::string, std::string> kExtensionMap = {
    // Images
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".webp", "image/webp"},
    
    // Audio
    {".mp3", "audio/mpeg"},
    {".wav", "audio/wav"},
    {".ogg", "audio/ogg"},
    {".m4a", "audio/mp4"},
    {".aac", "audio/aac"},
    {".opus", "audio/opus"},
    
    // Video
    {".mp4", "video/mp4"},
    {".mov", "video/quicktime"},
    {".avi", "video/avi"},
    {".webm", "video/webm"},
    {".mkv", "video/x-matroska"},
    
    // Documents
    {".pdf", "application/pdf"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".txt", "text/plain"},
};

} // anonymous namespace

std::string DetectMimeTypeFromContent(const std::vector<uint8_t>& data) {
  if (data.empty()) {
    return "application/octet-stream";
  }
  
  // Check magic numbers
  for (const auto& [mime, magic] : kMagicNumbers) {
    if (data.size() >= magic.size()) {
      bool match = true;
      for (size_t i = 0; i < magic.size(); ++i) {
        if (data[i] != magic[i]) {
          match = false;
          break;
        }
      }
      if (match) {
        return mime;
      }
    }
  }
  
  return "application/octet-stream";
}

std::string DetectMimeTypeFromExtension(const std::string& path) {
  size_t dot_pos = path.find_last_of('.');
  if (dot_pos == std::string::npos) {
    return "application/octet-stream";
  }
  
  std::string ext = path.substr(dot_pos);
  // Convert to lowercase
  for (auto& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  
  auto it = kExtensionMap.find(ext);
  if (it != kExtensionMap.end()) {
    return it->second;
  }
  
  return "application/octet-stream";
}

MediaType GetMediaTypeFromMime(const std::string& mime) {
  if (mime.find("image/") == 0) {
    return MediaType::IMAGE;
  } else if (mime.find("audio/") == 0 || mime.find("voice/") == 0) {
    return MediaType::VOICE;
  } else if (mime.find("video/") == 0) {
    return MediaType::VIDEO;
  } else {
    return MediaType::FILE;
  }
}

std::string GetExtensionFromMime(const std::string& mime) {
  static const std::map<std::string, std::string> mime_to_ext = {
    {"image/jpeg", ".jpg"},
    {"image/png", ".png"},
    {"image/gif", ".gif"},
    {"image/bmp", ".bmp"},
    {"image/webp", ".webp"},
    {"audio/mpeg", ".mp3"},
    {"audio/wav", ".wav"},
    {"audio/ogg", ".ogg"},
    {"video/mp4", ".mp4"},
    {"application/pdf", ".pdf"},
  };
  
  auto it = mime_to_ext.find(mime);
  if (it != mime_to_ext.end()) {
    return it->second;
  }
  
  return "";
}

} // namespace weixin::media
