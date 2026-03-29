#pragma once

#include "api/api_client.hpp"
#include "api/api_types.hpp"
#include <string>
#include <vector>

namespace weixin::messaging {

// Send media messages (images, files, voice)
class MediaSender {
public:
  MediaSender(api::APIClient& api_client);
  
  // Send image message
  bool SendImage(const std::string& user_id,
                 const std::string& context_token,
                 const std::string& image_path);
  
  // Send file message
  bool SendFile(const std::string& user_id,
                const std::string& context_token,
                const std::string& file_path);
  
  // Send voice message (already transcoded to compatible format)
  bool SendVoice(const std::string& user_id,
                 const std::string& context_token,
                 const std::string& voice_path);
  
private:
  api::APIClient& api_client_;
  
  // Upload file to CDN and get media ID
  std::optional<std::string> UploadMediaFile(
      const std::string& file_path,
      api::UploadMediaType media_type);
};

} // namespace weixin::messaging
