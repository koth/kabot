#include "agent/agent_registry.hpp"

#include <chrono>
#include <utility>

#include "utils/logging.hpp"

namespace kabot::agent {
namespace {

std::string EffectiveChannelInstance(const kabot::bus::InboundMessage& msg) {
    return msg.channel_instance.empty() ? msg.channel : msg.channel_instance;
}

}  // namespace

AgentRegistry::AgentRegistry(kabot::bus::MessageBus& bus,
                             kabot::providers::LLMProvider& provider,
                             const kabot::config::Config& config,
                             kabot::cron::CronService* cron)
    : bus_(bus)
    , provider_(provider)
    , config_(config)
    , cron_(cron) {
    InitAgents();
}

void AgentRegistry::InitAgents() {
    agents_.clear();
    for (const auto& agent_config : config_.agents.instances) {
        if (agent_config.brave_api_key.empty()) {
            LOG_WARN("[agent] init name={} workspace={} brave_api_key=empty",
                     agent_config.name,
                     agent_config.workspace);
        } else {
            const auto size = agent_config.brave_api_key.size();
            const auto prefix = size > 4
                ? agent_config.brave_api_key.substr(0, 4)
                : agent_config.brave_api_key;
            LOG_INFO("[agent] init name={} workspace={} brave_api_key={}***",
                     agent_config.name,
                     agent_config.workspace,
                     prefix);
        }
        agents_.emplace(
            agent_config.name,
            std::make_unique<AgentLoop>(
                bus_,
                provider_,
                agent_config.workspace,
                static_cast<const kabot::config::AgentDefaults&>(agent_config),
                config_.qmd,
                cron_));
    }
}

void AgentRegistry::Start() {
    if (running_) {
        return;
    }
    running_ = true;
    worker_ = std::thread([this] { RunLoop(); });
}

void AgentRegistry::Stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    for (auto& [_, agent] : agents_) {
        agent->Stop();
    }
}

void AgentRegistry::RunLoop() {
    while (running_) {
        kabot::bus::InboundMessage msg{};
        if (!bus_.TryConsumeInbound(msg, std::chrono::milliseconds(1000))) {
            continue;
        }
        auto outbound = HandleInbound(std::move(msg));
        bus_.PublishOutbound(outbound);
    }
}

kabot::bus::OutboundMessage AgentRegistry::HandleInbound(kabot::bus::InboundMessage msg) {
    msg.agent_name = ResolveAgentName(msg);

    auto it = agents_.find(msg.agent_name);
    if (it == agents_.end()) {
        kabot::bus::OutboundMessage outbound{};
        outbound.channel = msg.channel;
        outbound.channel_instance = EffectiveChannelInstance(msg);
        outbound.agent_name = msg.agent_name;
        outbound.chat_id = msg.chat_id;
        outbound.content = "No agent is configured to handle this message.";
        return outbound;
    }

    auto outbound = it->second->HandleInbound(msg);
    if (outbound.channel.empty()) {
        outbound.channel = msg.channel;
    }
    if (outbound.channel_instance.empty()) {
        outbound.channel_instance = EffectiveChannelInstance(msg);
    }
    if (outbound.agent_name.empty()) {
        outbound.agent_name = msg.agent_name;
    }
    if (outbound.chat_id.empty()) {
        outbound.chat_id = msg.chat_id;
    }
    return outbound;
}

std::string AgentRegistry::ProcessDirect(const std::string& agent_name,
                                         const std::string& content,
                                         const std::string& session_key) {
    const auto resolved = agent_name.empty() ? DefaultAgentName() : agent_name;
    auto it = agents_.find(resolved);
    if (it == agents_.end()) {
        return "No agent is configured to handle this request.";
    }
    return it->second->ProcessDirect(content, session_key);
}

const kabot::config::AgentInstanceConfig* AgentRegistry::GetAgentConfig(const std::string& name) const {
    return config_.FindAgent(name);
}

std::string AgentRegistry::ResolveAgentName(const kabot::bus::InboundMessage& msg) const {
    if (!msg.agent_name.empty() && config_.FindAgent(msg.agent_name)) {
        return msg.agent_name;
    }

    const auto channel_name = EffectiveChannelInstance(msg);
    if (const auto* channel = config_.FindChannelInstance(channel_name)) {
        if (!channel->binding.agent.empty() && config_.FindAgent(channel->binding.agent)) {
            return channel->binding.agent;
        }
    }

    return DefaultAgentName();
}

std::string AgentRegistry::DefaultAgentName() const {
    if (!config_.agents.instances.empty()) {
        return config_.agents.instances.front().name;
    }
    return "default";
}

}  // namespace kabot::agent
