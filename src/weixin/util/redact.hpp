#pragma once

#include <string>

namespace weixin::util {

// Redact sensitive information from logs
std::string RedactToken(const std::string& token);
std::string RedactUrlQuery(const std::string& url);
std::string TruncateBody(const std::string& body, size_t max_length = 1024);

} // namespace weixin::util
