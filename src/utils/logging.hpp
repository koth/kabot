#pragma once

#include <string>
#include <unordered_map>

namespace kabot::utils {

enum class LogLevel {
    kDebug,
    kInfo,
    kWarn,
    kError
};

inline const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kError: return "ERROR";
    }
    return "UNKNOWN";
}

struct LogMessage {
    LogLevel level;
    std::string message;
    std::unordered_map<std::string, std::string> fields;
};

struct LogConfig {
    LogLevel min_level = LogLevel::kInfo;
};

}  // namespace kabot::utils
