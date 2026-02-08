#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include "cron/cron_service.hpp"

namespace kabot::heartbeat {

class HeartbeatService {
public:
    using HeartbeatHandler = std::function<std::string(const std::string&)>;

    HeartbeatService(
        std::filesystem::path workspace,
        HeartbeatHandler on_heartbeat = {},
        kabot::cron::CronService::JobHandler on_cron_job = {},
        std::chrono::seconds interval = std::chrono::seconds(30 * 60),
        bool enabled = true,
        std::filesystem::path cron_store_path = {});
    ~HeartbeatService();

    void Start();
    void Stop();
    std::string TriggerNow();

    kabot::cron::CronService& Cron() { return cron_; }
    const kabot::cron::CronService& Cron() const { return cron_; }

private:
    std::filesystem::path workspace_;
    HeartbeatHandler on_heartbeat_;
    std::chrono::seconds interval_;
    bool enabled_ = true;
    std::atomic<bool> running_{false};
    std::thread worker_;
    kabot::cron::CronService cron_;

    void RunLoop();
    void Tick();
    std::filesystem::path HeartbeatFile() const;
    std::string ReadHeartbeatFile() const;
    static bool IsHeartbeatEmpty(const std::string& content);
    static std::filesystem::path DefaultCronStorePath();
};

}  // namespace kabot::heartbeat
