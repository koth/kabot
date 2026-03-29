#include "util/redact.hpp"

#include <regex>

namespace weixin::util {

std::string RedactToken(const std::string& token) {
  if (token.length() <= 8) {
    return "***";
  }
  return token.substr(0, 4) + "..." + token.substr(token.length() - 4);
}

std::string RedactUrlQuery(const std::string& url) {
  // Remove query parameters from URL for safe logging
  size_t query_pos = url.find('?');
  if (query_pos == std::string::npos) {
    return url;
  }
  return url.substr(0, query_pos) + "?[REDACTED]";
}

std::string TruncateBody(const std::string& body, size_t max_length) {
  if (body.length() <= max_length) {
    return body;
  }
  return body.substr(0, max_length) + "...[truncated]";
}

} // namespace weixin::util
