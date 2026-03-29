#pragma once

#include <string>

namespace weixin::messaging {

// Convert Markdown to plain text for Weixin compatibility
class MessageSender {
public:
  // Convert Markdown formatting to plain text
  static std::string MarkdownToText(const std::string& markdown);
  
  // Truncate message if too long
  static std::string TruncateMessage(const std::string& text, size_t max_length = 4096);
};

} // namespace weixin::messaging
