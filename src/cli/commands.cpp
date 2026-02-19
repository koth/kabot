#include <atomic>
#include <chrono>
#include <csignal>
#include <cerrno>
#include <filesystem>
#include <fstream>
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
    kabot::agent::AgentLoop agent(
        bus,
        *provider,
        config.agents.defaults.workspace,
        config.agents.defaults,
        config.qmd);

    kabot::heartbeat::HeartbeatService heartbeat(
        config.agents.defaults.workspace,
        [&agent](const std::string& prompt) {
            return agent.ProcessDirect(prompt, "heartbeat");
        },
        [&agent, &bus](const kabot::cron::CronJob& job) {
            const auto response = agent.ProcessDirect(
                job.payload.message,
                "cron:" + job.id);
            if (job.payload.deliver && !job.payload.to.empty()) {
                kabot::bus::OutboundMessage outbound{};
                outbound.channel = job.payload.channel.empty() ? "telegram" : job.payload.channel;
                outbound.chat_id = job.payload.to;
                outbound.content = response;
                bus.PublishOutbound(outbound);
            }
            return response;
        },
        std::chrono::seconds(config.heartbeat.interval_s),
        config.heartbeat.enabled,
        config.heartbeat.cron_store_path);

    kabot::channels::ChannelManager channels(config, bus);

    struct sigaction action {};
    action.sa_handler = HandleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);

    std::thread agent_thread([&agent]() { agent.Run(); });
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
