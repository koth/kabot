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
#include <unistd.h>

#include "agent/agent_loop.hpp"
#include "bus/message_bus.hpp"
#include "channels/channel_manager.hpp"
#include "config/config_loader.hpp"
#include "heartbeat/heartbeat_service.hpp"
#include "providers/llm_provider.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"

namespace {

std::atomic<bool> g_running{true};
volatile std::sig_atomic_t g_signal = 0;
const char* g_argv0 = nullptr;

std::filesystem::path GetHomePath() {
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".");
}

std::filesystem::path GetPidFilePath() {
    return GetHomePath() / ".kabot" / "gateway.pid";
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

bool IsProcessRunning(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (::kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

std::optional<pid_t> ReadPidFile() {
    const auto path = GetPidFilePath();
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }
    pid_t pid = 0;
    input >> pid;
    if (pid <= 0) {
        return std::nullopt;
    }
    return pid;
}

bool WritePidFile(pid_t pid) {
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

bool WaitForExit(pid_t pid, std::chrono::seconds timeout) {
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
    auto provider = kabot::providers::CreateProvider(config);
    if (!provider) {
        std::cout << "Failed to create provider." << std::endl;
        return 1;
    }

    const auto existing_pid = ReadPidFile();
    if (existing_pid && IsProcessRunning(*existing_pid)) {
        std::cout << "kabot gateway already running (pid=" << *existing_pid << ")" << std::endl;
        return 1;
    }
    RemovePidFile();

    if (!WritePidFile(::getpid())) {
        std::cout << "Failed to write gateway pid file." << std::endl;
        return 1;
    }

    kabot::bus::MessageBus bus;
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
        [&on_cron, &bus](const kabot::cron::CronJob& job) {
            if (on_cron) {
                return on_cron(job);
            }
            if (job.payload.deliver && !job.payload.to.empty()) {
                kabot::bus::OutboundMessage outbound{};
                outbound.channel = job.payload.channel.empty() ? "telegram" : job.payload.channel;
                outbound.chat_id = job.payload.to;
                outbound.content = job.payload.message;
                bus.PublishOutbound(outbound);
            }
            return job.payload.message;
        },
        std::chrono::seconds(config.heartbeat.interval_s),
        config.heartbeat.enabled,
        config.heartbeat.cron_store_path);

    kabot::agent::AgentLoop agent(
        bus,
        *provider,
        config.agents.defaults.workspace,
        config.agents.defaults,
        config.qmd,
        &heartbeat.Cron());

    on_heartbeat = [&agent](const std::string& prompt) {
        return agent.ProcessDirect(prompt, "heartbeat");
    };
    on_cron = [&agent, &bus](const kabot::cron::CronJob& job) {
        std::cout << "[cron] job payload deliver=" << (job.payload.deliver ? "true" : "false")
                  << " channel=" << (job.payload.channel.empty() ? "(empty)" : job.payload.channel)
                  << " to=" << (job.payload.to.empty() ? "(empty)" : job.payload.to)
                  << " message=" << job.payload.message << std::endl;
        if (job.payload.deliver) {
            kabot::bus::OutboundMessage outbound{};
            outbound.channel = job.payload.channel.empty() ? "lark" : job.payload.channel;
            outbound.chat_id = job.payload.to;
            outbound.content = job.payload.message;
            bus.PublishOutbound(outbound);
            return job.payload.message;
        }else{
            const auto response = agent.ProcessDirect(
            job.payload.message,
            "cron:" + job.id);
            kabot::bus::OutboundMessage outbound{};
            outbound.channel = job.payload.channel.empty() ? "lark" : job.payload.channel;
            outbound.chat_id = job.payload.to;
            outbound.content = response;
            bus.PublishOutbound(outbound);
            return response;
        }
    };

    kabot::channels::ChannelManager channels(config, bus);

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

    struct sigaction action {};
    action.sa_handler = HandleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);

    std::thread agent_thread([&agent]() { agent.Run(); });
    const std::string cron_http_host = config.heartbeat.cron_http_host;
    const int cron_http_port = config.heartbeat.cron_http_port;
    std::thread http_thread([&http_server, cron_http_host, cron_http_port]() {
        const bool ok = http_server.listen(cron_http_host, cron_http_port);
        if (!ok) {
            std::cerr << "[cron] http server failed to listen on "
                      << cron_http_host << ":" << cron_http_port << std::endl;
        }
    });
    channels.StartAll();
    heartbeat.Start();

    std::cout << "kabot gateway started. Press Ctrl+C to stop." << std::endl;
    bool shutdown_guard_started = false;
    bool restart_requested = false;
    while (g_running.load()) {
        if (g_signal != 0) {
            if (g_signal == SIGHUP) {
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
    heartbeat.Stop();
    channels.StopAll();
    agent.Stop();
    if (agent_thread.joinable()) {
        agent_thread.join();
    }
    RemovePidFile();
    if (restart_requested && g_argv0) {
        const char* args[] = {g_argv0, "gateway", nullptr};
        ::execv(g_argv0, const_cast<char* const*>(args));
        std::cout << "Failed to restart gateway." << std::endl;
        return 1;
    }
    return 0;
}

int RestartGateway(const char* argv0) {
    const auto pid = ReadPidFile();
    if (pid && IsProcessRunning(*pid)) {
        ::kill(*pid, SIGTERM);
        WaitForExit(*pid, std::chrono::seconds(8));
    }
    RemovePidFile();

    const char* args[] = {argv0, "gateway", nullptr};
    ::execv(argv0, const_cast<char* const*>(args));
    std::cout << "Failed to restart gateway." << std::endl;
    return 1;
}

int HupGateway() {
    const auto pid = ReadPidFile();
    if (!pid || !IsProcessRunning(*pid)) {
        std::cout << "kabot gateway not running." << std::endl;
        return 1;
    }
    ::kill(*pid, SIGHUP);
    return 0;
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

    if (argc < 2) {
        std::cout << "Usage: kabot_cli gateway | kabot_cli restart | kabot_cli hup | kabot_cli \"message\"" << std::endl;
        return 1;
    }

    std::string message = argv[1];

    auto config = kabot::config::LoadConfig();
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
        std::cout << "[empty response]" << std::endl;
        return 0;
    }

    std::cout << response.content << std::endl;
    return 0;
}
