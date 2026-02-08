#include "heartbeat/heartbeat_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace kabot::heartbeat {
namespace {

constexpr const char* kHeartbeatPrompt =
    "Read HEARTBEAT.md in your workspace (if it exists).\n"
    "Follow any instructions or tasks listed there.\n"
    "If nothing needs attention, reply with just: HEARTBEAT_OK";

std::filesystem::path GetHomePath() {
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
#endif
    return std::filesystem::path(home ? home : ".");
}

std::string NormalizeToken(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '_'), value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

}  // namespace

HeartbeatService::HeartbeatService(
    std::filesystem::path workspace,
    HeartbeatHandler on_heartbeat,
    kabot::cron::CronService::JobHandler on_cron_job,
    std::chrono::seconds interval,
    bool enabled,
    std::filesystem::path cron_store_path)
    : workspace_(std::move(workspace))
    , on_heartbeat_(std::move(on_heartbeat))
    , interval_(interval)
    , enabled_(enabled)
    , cron_(
        cron_store_path.empty() ? DefaultCronStorePath() : std::move(cron_store_path),
        std::move(on_cron_job)) {}

HeartbeatService::~HeartbeatService() {
    Stop();
}

void HeartbeatService::Start() {
    if (!enabled_ || running_.exchange(true)) {
        return;
    }
    cron_.Start();
    worker_ = std::thread([this]() { RunLoop(); });
}

void HeartbeatService::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    cron_.Stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string HeartbeatService::TriggerNow() {
    if (!on_heartbeat_) {
        return {};
    }
    return on_heartbeat_(kHeartbeatPrompt);
}

void HeartbeatService::RunLoop() {
    while (running_) {
        auto sleep_duration = interval_;
        const auto next_wake = cron_.GetNextWakeMs();
        if (next_wake.has_value()) {
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            if (next_wake.value() > now_ms) {
                const auto delta_ms = next_wake.value() - now_ms;
                sleep_duration = std::min(
                    interval_,
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::milliseconds(delta_ms)));
            }
        }
        std::this_thread::sleep_for(sleep_duration);
        if (!running_) {
            break;
        }
        Tick();
    }
}

void HeartbeatService::Tick() {
    cron_.RunDueJobs();

    auto content = ReadHeartbeatFile();
    if (IsHeartbeatEmpty(content)) {
        return;
    }
    if (!on_heartbeat_) {
        return;
    }

    const auto response = on_heartbeat_(kHeartbeatPrompt);
    const auto normalized = NormalizeToken(response);
    if (normalized.find("HEARTBEATOK") != std::string::npos) {
        return;
    }
}

std::filesystem::path HeartbeatService::HeartbeatFile() const {
    return workspace_ / "HEARTBEAT.md";
}

std::string HeartbeatService::ReadHeartbeatFile() const {
    const auto file = HeartbeatFile();
    if (!std::filesystem::exists(file)) {
        return {};
    }
    try {
        std::ifstream input(file);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    } catch (...) {
        return {};
    }
}

bool HeartbeatService::IsHeartbeatEmpty(const std::string& content) {
    if (content.empty()) {
        return true;
    }
    const std::vector<std::string> skip_patterns = {"- [ ]", "* [ ]", "- [x]", "* [x]"};
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char c) {
            return !std::isspace(c);
        }));
        line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char c) {
            return !std::isspace(c);
        }).base(), line.end());
        if (line.empty() || line.rfind("#", 0) == 0 || line.rfind("<!--", 0) == 0) {
            continue;
        }
        if (std::find(skip_patterns.begin(), skip_patterns.end(), line) != skip_patterns.end()) {
            continue;
        }
        return false;
    }
    return true;
}

std::filesystem::path HeartbeatService::DefaultCronStorePath() {
    return GetHomePath() / ".kabot" / "cron" / "jobs.json";
}

}  // namespace kabot::heartbeat
