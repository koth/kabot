#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "cron/cron_types.hpp"

namespace kabot::cron {

class CronService {
public:
    struct Status {
        bool enabled = false;
        std::size_t jobs = 0;
        std::optional<long long> next_wake_at_ms;
    };

    using JobHandler = std::function<std::string(const CronJob&)>;

    explicit CronService(std::filesystem::path store_path, JobHandler on_job = {});

    void Start();
    void Stop();

    std::vector<CronJob> ListJobs(bool include_disabled = false);
    CronJob AddJob(const CronJob& job);
    bool RemoveJob(const std::string& job_id);
    std::optional<CronJob> EnableJob(const std::string& job_id, bool enabled = true);
    bool RunJob(const std::string& job_id, bool force = false);
    Status GetStatus() const;

    void RunDueJobs();
    std::optional<long long> GetNextWakeMs() const;

private:
    void LoadStore();
    void SaveStore();
    void RecomputeNextRuns();
    bool ExecuteJob(CronJob& job);
    static std::optional<long long> ComputeNextRun(const CronSchedule& schedule, long long now_ms);
    static long long NowMs();

    std::filesystem::path store_path_;
    JobHandler on_job_;
    bool running_ = false;
    bool loaded_ = false;
    CronStore store_;
};

}  // namespace kabot::cron
