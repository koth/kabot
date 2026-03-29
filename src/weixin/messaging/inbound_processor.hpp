#pragma once

#include "api/api_types.hpp"
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace weixin::messaging {

// Process inbound messages
class InboundProcessor {
public:
  struct ProcessedMessage {
    std::string message_id;
    std::string user_id;
    std::string text;
    std::vector<std::string> media_paths;
    std::string context_token;
    std::unordered_map<std::string, std::string> metadata;
  };
  
  // Process a Weixin message
  std::optional<ProcessedMessage> Process(
      const api::WeixinMessage& msg,
      const std::string& media_output_dir);
  
private:
  std::string ExtractText(const api::WeixinMessage& msg);
  std::vector<std::string> DownloadMedia(
      const api::WeixinMessage& msg,
      const std::string& output_dir);
};

} // namespace weixin::messaging
