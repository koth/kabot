#include "messaging/message_sender.hpp"

#include <regex>

namespace weixin::messaging {

std::string MessageSender::MarkdownToText(const std::string& markdown) {
  std::string result = markdown;
  
  // Bold: **text** or __text__ -> text
  result = std::regex_replace(result, std::regex(R"(\*\*(.+?)\*\*)"), "$1");
  result = std::regex_replace(result, std::regex(R"(__(.+?)__)"), "$1");
  
  // Italic: *text* or _text_ -> text
  result = std::regex_replace(result, std::regex(R"(\*(.+?)\*)"), "$1");
  result = std::regex_replace(result, std::regex(R"(_(.+?)_)"), "$1");
  
  // Code: `text` -> text
  result = std::regex_replace(result, std::regex(R"(`(.+?)`)"), "$1");
  
  // Code block: ```text``` -> text
  result = std::regex_replace(result, std::regex(R"(```[\s\S]*?```)"), "[code]");
  
  // Links: [text](url) -> text (url)
  result = std::regex_replace(result, std::regex(R"(\[([^\]]+)\]\(([^)]+)\))"), "$1 ($2)");
  
  // Headers: # text -> text
  result = std::regex_replace(result, std::regex(R"(^#{1,6}\s+)"), "");
  
  // Strikethrough: ~~text~~ -> text
  result = std::regex_replace(result, std::regex(R"(~~(.+?)~~)"), "$1");
  
  return result;
}

std::string MessageSender::TruncateMessage(const std::string& text, size_t max_length) {
  if (text.length() <= max_length) {
    return text;
  }
  
  return text.substr(0, max_length - 3) + "...";
}

} // namespace weixin::messaging
