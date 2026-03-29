#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace weixin::util {

// Generate unique message ID with timestamp + random bytes
std::string GenerateMessageId();

// Generate temporary filename
std::string GenerateTempFilename(const std::string& prefix = "weixin");

// Generate random bytes
std::vector<uint8_t> GenerateRandomBytes(size_t length);

// Generate random uint32
uint32_t GenerateRandomUint32();

// Get current timestamp in milliseconds
int64_t GetTimestampMs();

} // namespace weixin::util
