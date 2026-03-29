#include "util/random.hpp"

#include <random>
#include <sstream>
#include <iomanip>

namespace weixin::util {

std::string GenerateMessageId() {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
  
  auto random_bytes = GenerateRandomBytes(4);
  
  std::stringstream ss;
  ss << ms;
  for (auto b : random_bytes) {
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
  }
  
  return ss.str();
}

std::string GenerateTempFilename(const std::string& prefix) {
  auto timestamp = GetTimestampMs();
  auto random_bytes = GenerateRandomBytes(4);
  
  std::stringstream ss;
  ss << prefix << "_" << timestamp;
  for (auto b : random_bytes) {
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
  }
  
  return ss.str();
}

std::vector<uint8_t> GenerateRandomBytes(size_t length) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> dis(0, 255);
  
  std::vector<uint8_t> result(length);
  for (size_t i = 0; i < length; ++i) {
    result[i] = static_cast<uint8_t>(dis(gen));
  }
  
  return result;
}

uint32_t GenerateRandomUint32() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<uint32_t> dis(
      0, std::numeric_limits<uint32_t>::max());
  
  return dis(gen);
}

int64_t GetTimestampMs() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
}

} // namespace weixin::util
