#pragma once

#include <string>
#include <unordered_map>

#include "spdlog/spdlog.h"

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

LogLevel ParseLogLevel(const std::string& value);

struct LogMessage {
    LogLevel level;
    std::string message;
    std::unordered_map<std::string, std::string> fields;
};

struct LogConfig {
    LogLevel min_level = LogLevel::kInfo;
    bool enable_stdout = true;
    std::string log_file;
};

void InitLogging(const LogConfig& config = {});

#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)

}  // namespace kabot::utils
