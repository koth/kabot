#include "agent/tools/cron.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>

#include "nlohmann/json.hpp"

namespace kabot::agent::tools {
namespace {

std::string GetParam(const std::unordered_map<std::string, std::string>& params,
                     const std::string& name) {
    auto it = params.find(name);
    if (it == params.end()) {
        return {};
    }
    return it->second;
}

std::string GetParamAlias(const std::unordered_map<std::string, std::string>& params,
                          const std::string& primary,
                          const std::string& fallback) {
    auto value = GetParam(params, primary);
    if (!value.empty()) {
        return value;
    }
    return GetParam(params, fallback);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::optional<long long> ParseLongLong(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoll(value);
    } catch (...) {
        return std::nullopt;
    }
}

bool ParseBool(const std::string& value, bool fallback = false) {
    if (value.empty()) {
        return fallback;
    }
    const auto lowered = ToLower(value);
    return lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "y";
}

std::optional<long long> ParseIsoMs(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    std::tm tm{};
    std::istringstream ss(value);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        return std::nullopt;
    }
    const auto seconds = std::mktime(&tm);
    if (seconds < 0) {
        return std::nullopt;
    }
    return static_cast<long long>(seconds) * 1000;
}

nlohmann::json BuildScheduleJson(const kabot::cron::CronSchedule& schedule) {
    nlohmann::json json = nlohmann::json::object();
    switch (schedule.kind) {
        case kabot::cron::CronScheduleKind::At:
            json["kind"] = "at";
            break;
        case kabot::cron::CronScheduleKind::Every:
            json["kind"] = "every";
            break;
        case kabot::cron::CronScheduleKind::Cron:
            json["kind"] = "cron";
            break;
    }
    json["at_ms"] = schedule.at_ms.has_value() ? nlohmann::json(*schedule.at_ms) : nlohmann::json(nullptr);
    json["every_ms"] = schedule.every_ms.has_value() ? nlohmann::json(*schedule.every_ms) : nlohmann::json(nullptr);
    json["expr"] = schedule.expr.empty() ? nlohmann::json(nullptr) : nlohmann::json(schedule.expr);
    json["tz"] = schedule.tz.empty() ? nlohmann::json(nullptr) : nlohmann::json(schedule.tz);
    return json;
}

nlohmann::json BuildPayloadJson(const kabot::cron::CronPayload& payload) {
    return {
        {"kind", payload.kind},
        {"message", payload.message},
        {"deliver", payload.deliver},
        {"channel", payload.channel.empty() ? nlohmann::json(nullptr) : nlohmann::json(payload.channel)},
        {"to", payload.to.empty() ? nlohmann::json(nullptr) : nlohmann::json(payload.to)}
    };
}

nlohmann::json BuildStateJson(const kabot::cron::CronJobState& state) {
    nlohmann::json json = nlohmann::json::object();
    json["next_run_at_ms"] = state.next_run_at_ms.has_value() ? nlohmann::json(*state.next_run_at_ms)
                                                                : nlohmann::json(nullptr);
    json["last_run_at_ms"] = state.last_run_at_ms.has_value() ? nlohmann::json(*state.last_run_at_ms)
                                                                : nlohmann::json(nullptr);
    json["last_status"] = state.last_status.empty() ? nlohmann::json(nullptr) : nlohmann::json(state.last_status);
    json["last_error"] = state.last_error.empty() ? nlohmann::json(nullptr) : nlohmann::json(state.last_error);
    return json;
}

}  // namespace

CronTool::CronTool(kabot::cron::CronService* cron)
    : cron_(cron) {}

std::string CronTool::ParametersJson() const {
    return R"({"type":"object","properties":{"action":{"type":"string","enum":["add","list","remove","enable","disable","run","status"]},"job_id":{"type":"string"},"id":{"type":"string"},"name":{"type":"string"},"mode":{"type":"string","enum":["reminder","task"]},"kind":{"type":"string","enum":["at","every","cron"]},"at":{"type":"string","description":"ISO local time: YYYY-MM-DDTHH:MM:SS"},"at_ms":{"type":"integer"},"every_seconds":{"type":"integer"},"every_ms":{"type":"integer"},"every_s":{"type":"integer"},"cron_expr":{"type":"string"},"expr":{"type":"string"},"tz":{"type":"string"},"message":{"type":"string"},"deliver":{"type":"boolean"},"channel":{"type":"string"},"to":{"type":"string"},"delete_after_run":{"type":"boolean"},"force":{"type":"boolean"},"enabled":{"type":"boolean"}},"required":["action"]})";
}

std::string CronTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    if (!cron_) {
        return "Error: cron service not configured";
    }
    const auto action_raw = GetParam(params, "action");
    if (action_raw.empty()) {
        return "Error: action is required";
    }
    const auto action = ToLower(action_raw);

    if (action == "status") {
        const auto status = cron_->GetStatus();
        nlohmann::json json = {
            {"enabled", status.enabled},
            {"jobs", status.jobs},
            {"next_wake_at_ms", status.next_wake_at_ms.has_value() ? nlohmann::json(*status.next_wake_at_ms)
                                                                     : nlohmann::json(nullptr)}
        };
        return json.dump(2);
    }

    if (action == "list") {
        nlohmann::json json = nlohmann::json::array();
        const auto jobs = cron_->ListJobs(true);
        for (const auto& job : jobs) {
            json.push_back({
                {"id", job.id},
                {"name", job.name.empty() ? nlohmann::json(nullptr) : nlohmann::json(job.name)},
                {"enabled", job.enabled},
                {"schedule", BuildScheduleJson(job.schedule)},
                {"payload", BuildPayloadJson(job.payload)},
                {"state", BuildStateJson(job.state)},
                {"delete_after_run", job.delete_after_run}
            });
        }
        return json.dump(2);
    }

    if (action == "remove") {
        const auto id = GetParamAlias(params, "job_id", "id");
        if (id.empty()) {
            return "Error: id is required";
        }
        const bool ok = cron_->RemoveJob(id);
        return ok ? "OK" : "Error: job not found";
    }

    if (action == "enable" || action == "disable") {
        const auto id = GetParamAlias(params, "job_id", "id");
        if (id.empty()) {
            return "Error: id is required";
        }
        bool enabled = (action == "enable");
        const auto enabled_param = GetParam(params, "enabled");
        if (!enabled_param.empty()) {
            enabled = ParseBool(enabled_param, enabled);
        }
        const auto updated = cron_->EnableJob(id, enabled);
        if (!updated.has_value()) {
            return "Error: job not found";
        }
        nlohmann::json json = {
            {"id", updated->id},
            {"enabled", updated->enabled},
            {"next_run_at_ms", updated->state.next_run_at_ms.has_value() ? nlohmann::json(*updated->state.next_run_at_ms)
                                                                         : nlohmann::json(nullptr)}
        };
        return json.dump(2);
    }

    if (action == "run") {
        const auto id = GetParamAlias(params, "job_id", "id");
        if (id.empty()) {
            return "Error: id is required";
        }
        const bool force = ParseBool(GetParam(params, "force"), false);
        const bool ok = cron_->RunJob(id, force);
        return ok ? "OK" : "Error: job not found or disabled";
    }

    if (action == "add") {
        kabot::cron::CronJob job;
        job.name = GetParam(params, "name");
        job.delete_after_run = ParseBool(GetParam(params, "delete_after_run"), false);

        const auto mode = ToLower(GetParam(params, "mode"));
        if (mode == "reminder") {
            job.payload.kind = "reminder";
            if (GetParam(params, "deliver").empty()) {
                job.payload.deliver = true;
            }
        } else {
            job.payload.kind = "agent_turn";
        }
        job.payload.message = GetParam(params, "message");
        if (job.payload.message.empty()) {
            return "Error: message is required";
        }
        job.payload.deliver = ParseBool(GetParam(params, "deliver"), false);
        job.payload.channel = GetParam(params, "channel");
        job.payload.to = GetParam(params, "to");

        const auto kind = ToLower(GetParam(params, "kind"));
        if (kind.empty() || kind == "every") {
            job.schedule.kind = kabot::cron::CronScheduleKind::Every;
            auto every_ms = ParseLongLong(GetParam(params, "every_ms"));
            if (!every_ms.has_value()) {
                const auto every_s = ParseLongLong(GetParamAlias(params, "every_seconds", "every_s"));
                if (every_s.has_value()) {
                    every_ms = every_s.value() * 1000;
                }
            }
            if (!every_ms.has_value() || every_ms.value() <= 0) {
                return "Error: every_ms or every_s is required for kind=every";
            }
            job.schedule.every_ms = every_ms;
        } else if (kind == "at") {
            job.schedule.kind = kabot::cron::CronScheduleKind::At;
            auto at_ms = ParseLongLong(GetParam(params, "at_ms"));
            if (!at_ms.has_value()) {
                at_ms = ParseIsoMs(GetParam(params, "at"));
            }
            if (!at_ms.has_value()) {
                return "Error: at or at_ms is required for kind=at";
            }
            job.schedule.at_ms = at_ms;
            if (GetParam(params, "delete_after_run").empty()) {
                job.delete_after_run = true;
            }
        } else if (kind == "cron") {
            job.schedule.kind = kabot::cron::CronScheduleKind::Cron;
            job.schedule.expr = GetParamAlias(params, "cron_expr", "expr");
            if (job.schedule.expr.empty()) {
                return "Error: expr is required for kind=cron";
            }
            job.schedule.tz = GetParam(params, "tz");
        } else {
            return "Error: invalid kind";
        }

        const auto added = cron_->AddJob(job);
        nlohmann::json json = {
            {"id", added.id},
            {"enabled", added.enabled},
            {"next_run_at_ms", added.state.next_run_at_ms.has_value() ? nlohmann::json(*added.state.next_run_at_ms)
                                                                      : nlohmann::json(nullptr)}
        };
        return json.dump(2);
    }

    return "Error: unsupported action";
}

}  // namespace kabot::agent::tools
