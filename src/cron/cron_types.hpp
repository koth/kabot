#pragma once

#include <optional>
#include <string>
#include <vector>

namespace kabot::cron {

enum class CronScheduleKind {
    At,
    Every,
    Cron
};

struct CronSchedule {
    CronScheduleKind kind = CronScheduleKind::Every;
    std::optional<long long> at_ms;
    std::optional<long long> every_ms;
    std::string expr;
    std::string tz;
};

struct CronPayload {
    std::string kind = "agent_turn";
    std::string message;
    bool deliver = false;
    std::string channel;
    std::string to;
};

struct CronJobState {
    std::optional<long long> next_run_at_ms;
    std::optional<long long> last_run_at_ms;
    std::string last_status;
    std::string last_error;
};

struct CronJob {
    std::string id;
    std::string name;
    bool enabled = true;
    CronSchedule schedule;
    CronPayload payload;
    CronJobState state;
    long long created_at_ms = 0;
    long long updated_at_ms = 0;
    bool delete_after_run = false;
};

struct CronStore {
    int version = 1;
    std::vector<CronJob> jobs;
};

}  // namespace kabot::cron
