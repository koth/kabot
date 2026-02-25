#include "cron/cron_service.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <random>
#include <utility>

#include "croncpp.h"

#include "nlohmann/json.hpp"

namespace kabot::cron {
namespace {

std::string GenerateId() {
    static const char* kChars = "0123456789abcdef";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id;
    id.reserve(8);
    for (int i = 0; i < 8; ++i) {
        id.push_back(kChars[dist(gen)]);
    }
    return id;
}

std::string ScheduleKindToString(CronScheduleKind kind) {
    switch (kind) {
        case CronScheduleKind::At:
            return "at";
        case CronScheduleKind::Every:
            return "every";
        case CronScheduleKind::Cron:
            return "cron";
    }
    return "every";
}

CronScheduleKind ScheduleKindFromString(const std::string& value) {
    if (value == "at") {
        return CronScheduleKind::At;
    }
    if (value == "cron") {
        return CronScheduleKind::Cron;
    }
    return CronScheduleKind::Every;
}

}  // namespace

CronService::CronService(std::filesystem::path store_path, JobHandler on_job)
    : store_path_(std::move(store_path)), on_job_(std::move(on_job)) {}

void CronService::Start() {
    running_ = true;
    LoadStore();
    RecomputeNextRuns();
    SaveStore();
}

void CronService::Stop() {
    running_ = false;
}

std::vector<CronJob> CronService::ListJobs(bool include_disabled) {
    LoadStore();
    std::vector<CronJob> jobs;
    jobs.reserve(store_.jobs.size());
    for (const auto& job : store_.jobs) {
        if (include_disabled || job.enabled) {
            jobs.push_back(job);
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const CronJob& a, const CronJob& b) {
        const auto left = a.state.next_run_at_ms.value_or(std::numeric_limits<long long>::max());
        const auto right = b.state.next_run_at_ms.value_or(std::numeric_limits<long long>::max());
        return left < right;
    });
    return jobs;
}

CronJob CronService::AddJob(const CronJob& job) {
    LoadStore();
    CronJob added = job;
    const auto now = NowMs();
    if (added.id.empty()) {
        added.id = GenerateId();
    }
    added.created_at_ms = now;
    added.updated_at_ms = now;
    added.state.next_run_at_ms = ComputeNextRun(added.schedule, now);
    store_.jobs.push_back(added);
    SaveStore();
    return added;
}

bool CronService::RemoveJob(const std::string& job_id) {
    LoadStore();
    const auto before = store_.jobs.size();
    store_.jobs.erase(std::remove_if(store_.jobs.begin(), store_.jobs.end(), [&](const CronJob& job) {
        return job.id == job_id;
    }), store_.jobs.end());
    const bool removed = store_.jobs.size() < before;
    if (removed) {
        SaveStore();
    }
    return removed;
}

std::optional<CronJob> CronService::EnableJob(const std::string& job_id, bool enabled) {
    LoadStore();
    for (auto& job : store_.jobs) {
        if (job.id == job_id) {
            job.enabled = enabled;
            job.updated_at_ms = NowMs();
            if (enabled) {
                job.state.next_run_at_ms = ComputeNextRun(job.schedule, NowMs());
            } else {
                job.state.next_run_at_ms.reset();
            }
            SaveStore();
            return job;
        }
    }
    return std::nullopt;
}

bool CronService::RunJob(const std::string& job_id, bool force) {
    LoadStore();
    for (auto& job : store_.jobs) {
        if (job.id == job_id) {
            if (!force && !job.enabled) {
                return false;
            }
            ExecuteJob(job);
            SaveStore();
            return true;
        }
    }
    return false;
}

CronService::Status CronService::GetStatus() const {
    Status status{};
    status.enabled = running_;
    status.jobs = store_.jobs.size();
    status.next_wake_at_ms = GetNextWakeMs();
    return status;
}

void CronService::RunDueJobs() {
    if (!running_) {
        return;
    }
    LoadStore();
    const auto now = NowMs();
    bool changed = false;
    for (auto& job : store_.jobs) {
        if (!job.enabled || !job.state.next_run_at_ms.has_value()) {
            continue;
        }
        if (now >= job.state.next_run_at_ms.value()) {
            const bool remove_job = ExecuteJob(job);
            if (remove_job) {
                job.enabled = false;
                job.id.clear();
            }
            changed = true;
        }
    }
    if (changed) {
        store_.jobs.erase(
            std::remove_if(store_.jobs.begin(), store_.jobs.end(), [](const CronJob& job) {
                return job.id.empty();
            }),
            store_.jobs.end());
    }
    if (changed) {
        SaveStore();
    }
}

std::optional<long long> CronService::GetNextWakeMs() const {
    std::optional<long long> next;
    for (const auto& job : store_.jobs) {
        if (!job.enabled || !job.state.next_run_at_ms.has_value()) {
            continue;
        }
        if (!next.has_value() || job.state.next_run_at_ms.value() < next.value()) {
            next = job.state.next_run_at_ms;
        }
    }
    return next;
}

void CronService::LoadStore() {
    if (loaded_) {
        return;
    }
    loaded_ = true;
    if (!std::filesystem::exists(store_path_)) {
        return;
    }
    try {
        std::ifstream input(store_path_);
        nlohmann::json data;
        input >> data;
        if (!data.is_object()) {
            return;
        }
        store_.jobs.clear();
        if (data.contains("jobs") && data["jobs"].is_array()) {
            for (const auto& item : data["jobs"]) {
                CronJob job;
                job.id = item.value("id", "");
                job.name = item.value("name", "");
                job.enabled = item.value("enabled", true);
                job.created_at_ms = item.value("createdAtMs", 0LL);
                job.updated_at_ms = item.value("updatedAtMs", 0LL);
                job.delete_after_run = item.value("deleteAfterRun", false);

                if (item.contains("schedule") && item["schedule"].is_object()) {
                    const auto& schedule = item["schedule"];
                    job.schedule.kind = ScheduleKindFromString(schedule.value("kind", "every"));
                    if (schedule.contains("atMs") && schedule["atMs"].is_number_integer()) {
                        job.schedule.at_ms = schedule["atMs"].get<long long>();
                    }
                    if (schedule.contains("everyMs") && schedule["everyMs"].is_number_integer()) {
                        job.schedule.every_ms = schedule["everyMs"].get<long long>();
                    }
                    job.schedule.expr = schedule.value("expr", "");
                    job.schedule.tz = schedule.value("tz", "");
                }

                if (item.contains("payload") && item["payload"].is_object()) {
                    const auto& payload = item["payload"];
                    job.payload.kind = payload.value("kind", "agent_turn");
                    job.payload.message = payload.value("message", "");
                    job.payload.deliver = payload.value("deliver", false);
                    job.payload.channel = payload.value("channel", "");
                    job.payload.to = payload.value("to", "");
                }

                if (item.contains("state") && item["state"].is_object()) {
                    const auto& state = item["state"];
                    if (state.contains("nextRunAtMs") && state["nextRunAtMs"].is_number_integer()) {
                        job.state.next_run_at_ms = state["nextRunAtMs"].get<long long>();
                    }
                    if (state.contains("lastRunAtMs") && state["lastRunAtMs"].is_number_integer()) {
                        job.state.last_run_at_ms = state["lastRunAtMs"].get<long long>();
                    }
                    job.state.last_status = state.value("lastStatus", "");
                    job.state.last_error = state.value("lastError", "");
                }

                store_.jobs.push_back(job);
            }
        }
    } catch (...) {
        store_.jobs.clear();
    }
}

void CronService::SaveStore() {
    std::filesystem::create_directories(store_path_.parent_path());
    nlohmann::json data;
    data["version"] = store_.version;
    data["jobs"] = nlohmann::json::array();
    for (const auto& job : store_.jobs) {
        nlohmann::json entry;
        entry["id"] = job.id;
        entry["name"] = job.name;
        entry["enabled"] = job.enabled;
        entry["createdAtMs"] = job.created_at_ms;
        entry["updatedAtMs"] = job.updated_at_ms;
        entry["deleteAfterRun"] = job.delete_after_run;

        nlohmann::json schedule;
        schedule["kind"] = ScheduleKindToString(job.schedule.kind);
        schedule["atMs"] = job.schedule.at_ms.has_value()
                                ? nlohmann::json(job.schedule.at_ms.value())
                                : nlohmann::json(nullptr);
        schedule["everyMs"] = job.schedule.every_ms.has_value()
                                   ? nlohmann::json(job.schedule.every_ms.value())
                                   : nlohmann::json(nullptr);
        schedule["expr"] = job.schedule.expr.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(job.schedule.expr);
        schedule["tz"] = job.schedule.tz.empty() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(job.schedule.tz);
        entry["schedule"] = schedule;

        nlohmann::json payload;
        payload["kind"] = job.payload.kind;
        payload["message"] = job.payload.message;
        payload["deliver"] = job.payload.deliver;
        payload["channel"] = job.payload.channel.empty() ? nlohmann::json(nullptr)
                                                          : nlohmann::json(job.payload.channel);
        payload["to"] = job.payload.to.empty() ? nlohmann::json(nullptr)
                                                : nlohmann::json(job.payload.to);
        entry["payload"] = payload;

        nlohmann::json state;
        state["nextRunAtMs"] = job.state.next_run_at_ms.has_value()
                                    ? nlohmann::json(job.state.next_run_at_ms.value())
                                    : nlohmann::json(nullptr);
        state["lastRunAtMs"] = job.state.last_run_at_ms.has_value()
                                    ? nlohmann::json(job.state.last_run_at_ms.value())
                                    : nlohmann::json(nullptr);
        state["lastStatus"] = job.state.last_status.empty() ? nlohmann::json(nullptr)
                                                            : nlohmann::json(job.state.last_status);
        state["lastError"] = job.state.last_error.empty() ? nlohmann::json(nullptr)
                                                          : nlohmann::json(job.state.last_error);
        entry["state"] = state;

        data["jobs"].push_back(entry);
    }

    std::ofstream output(store_path_);
    output << data.dump(2);
}

void CronService::RecomputeNextRuns() {
    const auto now = NowMs();
    bool changed = false;
    for (auto& job : store_.jobs) {
        if (job.enabled) {
            job.state.next_run_at_ms = ComputeNextRun(job.schedule, now);
            if (!job.state.next_run_at_ms.has_value() &&
                job.schedule.kind == CronScheduleKind::At &&
                job.schedule.at_ms.has_value() &&
                job.schedule.at_ms.value() <= now) {
                job.enabled = false;
                job.id.clear();
                changed = true;
            }
        }
    }
    if (changed) {
        store_.jobs.erase(
            std::remove_if(store_.jobs.begin(), store_.jobs.end(), [](const CronJob& job) {
                return job.id.empty();
            }),
            store_.jobs.end());
    }
}

bool CronService::ExecuteJob(CronJob& job) {
    const auto start = NowMs();
    const std::string job_id = job.id;
    const CronJob snapshot = job;
    std::string last_status;
    std::string last_error;
    try {
        if (on_job_) {
            on_job_(snapshot);
        }
        last_status = "ok";
        last_error.clear();
    } catch (const std::exception& ex) {
        last_status = "error";
        last_error = ex.what();
    } catch (...) {
        last_status = "error";
        last_error = "unknown error";
    }

    auto it = std::find_if(store_.jobs.begin(), store_.jobs.end(), [&](const CronJob& item) {
        return item.id == job_id;
    });
    if (it == store_.jobs.end()) {
        return true;
    }

    it->state.last_status = last_status;
    it->state.last_error = last_error;
    it->state.last_run_at_ms = start;
    it->updated_at_ms = NowMs();

    if (snapshot.schedule.kind == CronScheduleKind::At) {
        if (snapshot.delete_after_run) {
            return true;
        }
        it->enabled = false;
        it->state.next_run_at_ms.reset();
        return false;
    } else {
        it->state.next_run_at_ms = ComputeNextRun(snapshot.schedule, NowMs());
    }
    return false;
}

std::optional<long long> CronService::ComputeNextRun(const CronSchedule& schedule, long long now_ms) {
    if (schedule.kind == CronScheduleKind::At) {
        if (schedule.at_ms.has_value() && schedule.at_ms.value() > now_ms) {
            return schedule.at_ms;
        }
        return std::nullopt;
    }
    if (schedule.kind == CronScheduleKind::Every) {
        if (!schedule.every_ms.has_value() || schedule.every_ms.value() <= 0) {
            return std::nullopt;
        }
        return now_ms + schedule.every_ms.value();
    }
    if (schedule.kind == CronScheduleKind::Cron && !schedule.expr.empty()) {
        try {
            const auto cron_expr = ::cron::make_cron(schedule.expr);
            const auto now_tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(now_ms));
            const auto next_tp = ::cron::cron_next(cron_expr, now_tp);
            const auto next_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                next_tp.time_since_epoch()).count();
            return next_ms;
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

long long CronService::NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace kabot::cron
