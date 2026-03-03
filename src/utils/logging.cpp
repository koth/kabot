#include "utils/logging.hpp"

#include <vector>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace kabot::utils {
namespace {

LogLevel NormalizeLogLevel(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    if (value == "debug") {
        return LogLevel::kDebug;
    }
    if (value == "warn" || value == "warning") {
        return LogLevel::kWarn;
    }
    if (value == "error") {
        return LogLevel::kError;
    }
    return LogLevel::kInfo;
}

spdlog::level::level_enum ToSpdLevel(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug:
            return spdlog::level::debug;
        case LogLevel::kInfo:
            return spdlog::level::info;
        case LogLevel::kWarn:
            return spdlog::level::warn;
        case LogLevel::kError:
            return spdlog::level::err;
    }
    return spdlog::level::info;
}

}  // namespace

LogLevel ParseLogLevel(const std::string& value) {
    return NormalizeLogLevel(value);
}

void InitLogging(const LogConfig& config) {
    std::vector<spdlog::sink_ptr> sinks;
    if (config.enable_stdout) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    if (!config.log_file.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(config.log_file, true));
    }

    if (!sinks.empty()) {
        auto logger = std::make_shared<spdlog::logger>("kabot", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
    }
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] %v");
    spdlog::set_level(ToSpdLevel(config.min_level));
    spdlog::flush_on(spdlog::level::info);
}

}  // namespace kabot::utils
