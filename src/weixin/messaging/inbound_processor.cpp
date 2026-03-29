#include "messaging/inbound_processor.hpp"
#include "media/media_downloader.hpp"
#include "util/random.hpp"

#include <filesystem>

namespace weixin::messaging {

std::optional<InboundProcessor::ProcessedMessage> InboundProcessor::Process(
    const api::WeixinMessage& msg,
    const std::string& media_output_dir) {
  
  ProcessedMessage result;
  
  // Generate message ID
  if (msg.message_id.has_value()) {
    result.message_id = std::to_string(msg.message_id.value());
  } else {
    result.message_id = util::GenerateMessageId();
  }
  
  // Get user ID
  if (msg.from_user_id.has_value()) {
    result.user_id = msg.from_user_id.value();
  } else {
    return std::nullopt;
  }
  
  // Get context token
  if (msg.context_token.has_value()) {
    result.context_token = msg.context_token.value();
  }
  
  // Extract text content
  result.text = ExtractText(msg);
  
  // Download media files
  result.media_paths = DownloadMedia(msg, media_output_dir);
  
  // Set metadata
  result.metadata["message_id"] = result.message_id;
  result.metadata["user_id"] = result.user_id;
  
  return result;
}

std::string InboundProcessor::ExtractText(const api::WeixinMessage& msg) {
  for (const auto& item : msg.item_list) {
    if (item.type == api::MessageItemType::TEXT && item.text_item.has_value()) {
      return item.text_item.value().text;
    }
  }
  return "";
}

std::vector<std::string> InboundProcessor::DownloadMedia(
    const api::WeixinMessage& msg,
    const std::string& output_dir) {
  std::vector<std::string> paths;
  
  // TODO: Implement media download from CDN
  // This requires parsing CDN media info from the message
  
  return paths;
}

} // namespace weixin::messaging
