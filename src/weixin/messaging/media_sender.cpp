#include "messaging/media_sender.hpp"
#include "cdn/cdn_upload.hpp"
#include "media/mime_detector.hpp"

#include <fstream>

namespace weixin::messaging {

MediaSender::MediaSender(api::APIClient& api_client)
    : api_client_(api_client) {}

bool MediaSender::SendImage(const std::string& user_id,
                            const std::string& context_token,
                            const std::string& image_path) {
  auto media_id = UploadMediaFile(image_path, api::UploadMediaType::IMAGE);
  if (!media_id.has_value()) {
    return false;
  }
  
  // TODO: Send media message via API
  return true;
}

bool MediaSender::SendFile(const std::string& user_id,
                           const std::string& context_token,
                           const std::string& file_path) {
  auto media_id = UploadMediaFile(file_path, api::UploadMediaType::FILE);
  if (!media_id.has_value()) {
    return false;
  }
  
  // TODO: Send media message via API
  return true;
}

bool MediaSender::SendVoice(const std::string& user_id,
                            const std::string& context_token,
                            const std::string& voice_path) {
  auto media_id = UploadMediaFile(voice_path, api::UploadMediaType::VOICE);
  if (!media_id.has_value()) {
    return false;
  }
  
  // TODO: Send media message via API
  return true;
}

std::optional<std::string> MediaSender::UploadMediaFile(
    const std::string& file_path,
    api::UploadMediaType media_type) {
  
  // Read file
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
  
  // Get upload URL
  auto upload_url_result = api_client_.GetUploadUrl(media_type, data.size());
  if (!upload_url_result.success || !upload_url_result.data.has_value()) {
    return std::nullopt;
  }
  
  auto& upload_data = upload_url_result.data.value();
  
  // Upload to CDN
  auto upload_result = cdn::UploadFileToCdn(
      upload_data.upload_url, file_path);
  
  if (!upload_result.success) {
    return std::nullopt;
  }
  
  return upload_data.media_id;
}

} // namespace weixin::messaging
