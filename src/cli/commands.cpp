#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <cstdlib>
#include <cctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif
#include <Windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#endif

#include "agent/agent_registry.hpp"
#include "bus/message_bus.hpp"
#include "channels/channel_manager.hpp"
#include "config/config_loader.hpp"
#include "heartbeat/heartbeat_service.hpp"
#include "relay/relay_manager.hpp"
#include "session/session_manager.hpp"
#include "task/task_runtime.hpp"
#include "providers/llm_provider.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"
#include "utils/logging.hpp"

#ifdef KABOT_ENABLE_WEIXIN
#include "weixin/auth/login_qr.hpp"
#endif

namespace {

std::atomic<bool> g_running{true};
volatile std::sig_atomic_t g_signal = 0;
const char* g_argv0 = nullptr;

#ifdef _WIN32
using ProcessId = int;
constexpr int kReloadSignal = SIGBREAK;
#else
using ProcessId = pid_t;
constexpr int kReloadSignal = SIGHUP;
#endif

std::filesystem::path GetHomePath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return std::filesystem::path(home ? home : ".");
}

std::filesystem::path GetPidFilePath() {
    return GetHomePath() / ".kabot" / "gateway.pid";
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ResolveCronTo(
    const kabot::config::Config& config,
    const std::string& channel,
    const std::string& to) {
    if (ToLower(to) != "user") {
        return to;
    }
    if (const auto* instance = config.FindChannelInstance(channel)) {
        const auto& allow_from = instance->allow_from;
        if (!allow_from.empty()) {
            return allow_from.front();
        }
    }
    return to;
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
        {"agent", payload.agent.empty() ? nlohmann::json(nullptr) : nlohmann::json(payload.agent)},
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

bool IsProcessRunning(ProcessId pid) {
    if (pid <= 0) {
        return false;
    }
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) {
        return false;
    }
    DWORD exit_code = 0;
    const BOOL ok = GetExitCodeProcess(process, &exit_code);
    CloseHandle(process);
    return ok && exit_code == STILL_ACTIVE;
#else
    if (::kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
#endif
}

std::optional<ProcessId> ReadPidFile() {
    const auto path = GetPidFilePath();
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }
    ProcessId pid = 0;
    input >> pid;
    if (pid <= 0) {
        return std::nullopt;
    }
    return pid;
}

bool WritePidFile(ProcessId pid) {
    const auto path = GetPidFilePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << pid;
    return true;
}

void RemovePidFile() {
    const auto path = GetPidFilePath();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

bool WaitForExit(ProcessId pid, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!IsProcessRunning(pid)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return !IsProcessRunning(pid);
}

void HandleSignal(int signal) {
    g_signal = signal;
}

int RunGateway() {
    auto config = kabot::config::LoadConfig();
    const auto validation_errors = kabot::config::ValidateConfig(config);
    if (!validation_errors.empty()) {
        for (const auto& error : validation_errors) {
            std::cerr << "config error: " << error << std::endl;
        }
        return 1;
    }

    kabot::utils::LogConfig log_config;
    log_config.min_level = kabot::utils::ParseLogLevel(config.logging.level);
    log_config.log_file = config.logging.log_file;
    log_config.enable_stdout = config.logging.enable_stdout;
    kabot::utils::InitLogging(log_config);
    auto provider = kabot::providers::CreateProvider(config);
    if (!provider) {
        LOG_ERROR("Failed to create provider.");
        return 1;
    }

    const auto existing_pid = ReadPidFile();
    if (existing_pid && IsProcessRunning(*existing_pid)) {
        LOG_WARN("kabot gateway already running (pid={})", *existing_pid);
        return 1;
    }
    RemovePidFile();

#ifdef _WIN32
    if (!WritePidFile(_getpid())) {
#else
    if (!WritePidFile(::getpid())) {
#endif
        LOG_ERROR("Failed to write gateway pid file.");
        return 1;
    }

    kabot::bus::MessageBus bus;
    const std::string default_agent_name = config.agents.instances.empty()
        ? std::string("default")
        : config.agents.instances.front().name;
    std::function<std::string(const std::string&)> on_heartbeat;
    std::function<std::string(const kabot::cron::CronJob&)> on_cron;
    kabot::heartbeat::HeartbeatService heartbeat(
        config.agents.defaults.workspace,
        [&on_heartbeat](const std::string& prompt) {
            if (on_heartbeat) {
                return on_heartbeat(prompt);
            }
            return std::string("HEARTBEAT_OK");
        },
        [&on_cron, &bus, &config](const kabot::cron::CronJob& job) {
            if (on_cron) {
                return on_cron(job);
            }
            if (job.payload.deliver && !job.payload.to.empty()) {
                kabot::bus::OutboundMessage outbound{};
                outbound.channel = job.payload.channel.empty() ? "telegram" : job.payload.channel;
                outbound.channel_instance = outbound.channel;
                outbound.agent_name = job.payload.agent;
                outbound.chat_id = ResolveCronTo(config, outbound.channel_instance, job.payload.to);
                outbound.content = job.payload.message;
                bus.PublishOutbound(outbound);
            }
            return job.payload.message;
        },
        std::chrono::seconds(config.heartbeat.interval_s),
        config.heartbeat.enabled,
        config.heartbeat.cron_store_path);

    kabot::agent::AgentRegistry agents(
        bus,
        *provider,
        config,
        &heartbeat.Cron());
    kabot::channels::ChannelManager channels(config, bus);
    kabot::relay::RelayManager relay(config, agents);
    kabot::task::TaskRuntime task_runtime(config, agents, relay, &heartbeat.Cron());

    agents.SetInboundInterceptor([&task_runtime](kabot::bus::InboundMessage& msg,
                                                 kabot::bus::OutboundMessage& outbound) {
        return task_runtime.HandleInbound(msg, outbound);
    });
    agents.SetInboundPostProcessor([&task_runtime](const kabot::bus::InboundMessage& msg,
                                                   const kabot::bus::OutboundMessage& outbound) {
        task_runtime.ObserveInboundResult(msg, outbound);
    });

    on_heartbeat = [&agents, &default_agent_name](const std::string& prompt) {
        return agents.ProcessDirect(default_agent_name, prompt, "heartbeat:" + default_agent_name);
    };
    on_cron = [&agents, &bus, &config, &default_agent_name, &task_runtime](const kabot::cron::CronJob& job) {
        std::string task_runtime_response;
        if (task_runtime.HandleCron(job, task_runtime_response)) {
            return task_runtime_response;
        }
        const std::string resolved_agent_name = job.payload.agent.empty() ? default_agent_name : job.payload.agent;
        const std::string resolved_channel_name = job.payload.channel.empty() ? config.channels.instances.front().name : job.payload.channel;
        LOG_INFO("[cron] job payload deliver={} agent={} channel={} to={} message={}",
                 (job.payload.deliver ? "true" : "false"),
                 (resolved_agent_name.empty() ? "(empty)" : resolved_agent_name),
                 (resolved_channel_name.empty() ? "(empty)" : resolved_channel_name),
                 (job.payload.to.empty() ? "(empty)" : job.payload.to),
                 job.payload.message);
        if (job.payload.deliver) {
            kabot::bus::OutboundMessage outbound{};
            outbound.channel = resolved_channel_name;
            outbound.channel_instance = outbound.channel;
            outbound.agent_name = resolved_agent_name;
            outbound.chat_id = ResolveCronTo(config, outbound.channel_instance, job.payload.to);
            outbound.content = job.payload.message;
            bus.PublishOutbound(outbound);
            return job.payload.message;
        } else {
            const auto response = agents.ProcessDirect(
                resolved_agent_name,
                job.payload.message,
                "cron:" + resolved_agent_name + ":" + job.id);
            kabot::bus::OutboundMessage outbound{};
            outbound.channel = resolved_channel_name;
            outbound.channel_instance = outbound.channel;
            outbound.agent_name = resolved_agent_name;
            outbound.chat_id = ResolveCronTo(config, outbound.channel_instance, job.payload.to);
            outbound.content = response;
            bus.PublishOutbound(outbound);
            return response;
        }
    };

    kabot::session::SessionManager sessions(config.agents.defaults.workspace);

    httplib::Server http_server;
    http_server.Get("/cron", [&heartbeat](const httplib::Request&, httplib::Response& res) {
        nlohmann::json json = nlohmann::json::array();
        const auto jobs = heartbeat.Cron().ListJobs(true);
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
        res.set_content(json.dump(2), "application/json");
    });

    http_server.Get("/sessions", [&sessions](const httplib::Request&, httplib::Response& res) {
        nlohmann::json json = nlohmann::json::array();
        const auto list = sessions.ListSessions();
        for (const auto& info : list) {
            json.push_back({
                {"id", info.key},
                {"created_at", info.created_at},
                {"updated_at", info.updated_at}
            });
        }
        res.set_content(json.dump(2), "application/json");
    });

    http_server.Get("/task-runtime/state", [&task_runtime](const httplib::Request&, httplib::Response& res) {
        res.set_content(task_runtime.DumpStateJson(), "application/json");
    });

    http_server.Get(R"(/sessions/(.+))", [&sessions](const httplib::Request& req, httplib::Response& res) {
        if (req.matches.size() < 2) {
            res.status = 400;
            res.set_content("missing session id", "text/plain");
            return;
        }
        const auto session_id = httplib::detail::decode_url(req.matches[1], false);
        const auto session = sessions.Get(session_id);
        if (!session.has_value()) {
            res.status = 404;
            res.set_content("session not found", "text/plain");
            return;
        }
        nlohmann::json json = nlohmann::json::object();
        json["id"] = session->Key();
        json["created_at"] = session->CreatedAt();
        json["updated_at"] = session->UpdatedAt();
        json["messages"] = nlohmann::json::array();
        for (const auto& msg : session->Messages()) {
            nlohmann::json entry = {
                {"role", msg.role},
                {"content", msg.content},
                {"timestamp", msg.timestamp},
                {"name", msg.name.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.name)},
                {"tool_call_id", msg.tool_call_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.tool_call_id)},
                {"tool_calls", nlohmann::json::array()}
            };
            for (const auto& call : msg.tool_calls) {
                nlohmann::json args = nlohmann::json::object();
                for (const auto& [key, value] : call.arguments) {
                    args[key] = value;
                }
                entry["tool_calls"].push_back({
                    {"id", call.id},
                    {"name", call.name},
                    {"arguments", std::move(args)}
                });
            }
            json["messages"].push_back(std::move(entry));
        }
        res.set_content(json.dump(2), "application/json");
    });

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
#ifdef _WIN32
    std::signal(SIGBREAK, HandleSignal);
#else
    struct sigaction action {};
    action.sa_handler = HandleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
#endif

    const std::string cron_http_host = config.heartbeat.cron_http_host;
    const int cron_http_port = config.heartbeat.cron_http_port;
    std::thread http_thread([&http_server, cron_http_host, cron_http_port]() {
        const bool ok = http_server.listen(cron_http_host, cron_http_port);
        if (!ok) {
            LOG_ERROR("[cron] http server failed to listen on {}:{}", cron_http_host, cron_http_port);
        }
    });
    agents.Start();
    channels.StartAll();
    relay.Start();
    heartbeat.Start();
    task_runtime.Start();

    LOG_INFO("kabot gateway started. Press Ctrl+C to stop.");
    bool shutdown_guard_started = false;
    bool restart_requested = false;
    while (g_running.load()) {
        if (g_signal != 0) {
            if (g_signal == kReloadSignal) {
                restart_requested = true;
            }
            g_running.store(false);
            if (!shutdown_guard_started) {
                shutdown_guard_started = true;
                std::thread([] {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    std::_Exit(130);
                }).detach();
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    http_server.stop();
    if (http_thread.joinable()) {
        http_thread.join();
    }
    task_runtime.Stop();
    heartbeat.Stop();
    relay.Stop();
    channels.StopAll();
    agents.Stop();
    RemovePidFile();
    if (restart_requested && g_argv0) {
#ifdef _WIN32
        LOG_ERROR("Gateway restart via signal is not supported on Windows.");
        return 1;
#else
        const char* args[] = {g_argv0, "gateway", nullptr};
        ::execv(g_argv0, const_cast<char* const*>(args));
        LOG_ERROR("Failed to restart gateway.");
        return 1;
#endif
    }
    return 0;
}

int RestartGateway(const char* argv0) {
    const auto pid = ReadPidFile();
    if (pid && IsProcessRunning(*pid)) {
#ifdef _WIN32
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(*pid));
        if (process) {
            TerminateProcess(process, 0);
            CloseHandle(process);
        }
#else
        ::kill(*pid, SIGTERM);
#endif
        WaitForExit(*pid, std::chrono::seconds(8));
    }
    RemovePidFile();

#ifdef _WIN32
    LOG_ERROR("Restart command is not supported on Windows.");
    return 1;
#else
    const char* args[] = {argv0, "gateway", nullptr};
    ::execv(argv0, const_cast<char* const*>(args));
    LOG_ERROR("Failed to restart gateway.");
    return 1;
#endif
}

int HupGateway() {
    const auto pid = ReadPidFile();
    if (!pid || !IsProcessRunning(*pid)) {
        LOG_WARN("kabot gateway not running.");
        return 1;
    }
#ifdef _WIN32
    LOG_ERROR("HUP command is not supported on Windows.");
    return 1;
#else
    ::kill(*pid, SIGHUP);
    return 0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    g_argv0 = (argc > 0 ? argv[0] : nullptr);
    if (argc >= 2 && std::string(argv[1]) == "gateway") {
        return RunGateway();
    }

    if (argc >= 2 && std::string(argv[1]) == "restart") {
        return RestartGateway(argv[0]);
    }

    if (argc >= 2 && std::string(argv[1]) == "hup") {
        return HupGateway();
    }

#ifdef KABOT_ENABLE_WEIXIN
    if (argc >= 2 && std::string(argv[1]) == "weixin-login") {
        std::string account_id = (argc >= 3) ? argv[2] : "default";
        std::string base_url = "https://ilinkai.weixin.qq.com";
        
        std::cout << "Starting WeChat QR code login for account: " << account_id << std::endl;
        bool success = weixin::auth::PerformQRLogin(account_id, base_url);
        return success ? 0 : 1;
    }
#endif

    if (argc < 2) {
        LOG_INFO("Usage: kabot_cli gateway | kabot_cli restart | kabot_cli hup | kabot_cli weixin-login [account_id] | kabot_cli \"message\"");
        return 1;
    }

    std::string message = argv[1];

    auto config = kabot::config::LoadConfig();
    kabot::utils::LogConfig log_config;
    log_config.min_level = kabot::utils::ParseLogLevel(config.logging.level);
    log_config.log_file = config.logging.log_file;
    log_config.enable_stdout = config.logging.enable_stdout;
    kabot::utils::InitLogging(log_config);
    auto provider = kabot::providers::CreateProvider(config);

    std::vector<kabot::providers::Message> messages;
    kabot::providers::Message user{};
    user.role = "user";
    user.content = message;
    messages.push_back(user);

    auto response = provider->Chat(
        messages,
        {},
        config.agents.defaults.model,
        config.agents.defaults.max_tokens,
        config.agents.defaults.temperature);

    if (response.content.empty()) {
        LOG_INFO("[empty response]");
        return 0;
    }

    LOG_INFO("{}", response.content);
    return 0;
}
