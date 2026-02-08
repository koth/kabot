#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "agent/agent_loop.hpp"
#include "bus/message_bus.hpp"
#include "channels/channel_manager.hpp"
#include "config/config_loader.hpp"
#include "heartbeat/heartbeat_service.hpp"
#include "providers/llm_provider.hpp"

namespace {

std::atomic<bool> g_running{true};
volatile std::sig_atomic_t g_signal = 0;

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

    kabot::bus::MessageBus bus;
    kabot::agent::AgentLoop agent(
        bus,
        *provider,
        config.agents.defaults.workspace,
        config.agents.defaults);

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

    std::thread agent_thread([&agent]() { agent.Run(); });
    channels.StartAll();
    heartbeat.Start();

    std::cout << "kabot gateway started. Press Ctrl+C to stop." << std::endl;
    bool shutdown_guard_started = false;
    while (g_running.load()) {
        if (g_signal != 0) {
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
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "gateway") {
        return RunGateway();
    }

    if (argc < 2) {
        std::cout << "Usage: kabot_cli gateway | kabot_cli \"message\"" << std::endl;
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
