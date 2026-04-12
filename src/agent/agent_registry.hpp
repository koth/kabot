#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "agent/agent_loop.hpp"
#include "bus/message_bus.hpp"
#include "config/config_schema.hpp"
#include "providers/llm_provider.hpp"

namespace kabot::agent {

class AgentRegistry {
public:
    using InboundExecutionReporter = std::function<void(const std::string&,
                                                        DirectExecutionPhase,
                                                        bool,
                                                        const std::string&)>;
    using InboundInterceptor = std::function<bool(kabot::bus::InboundMessage&,
                                                  kabot::bus::OutboundMessage&)>;
    using InboundPostProcessor = std::function<void(const kabot::bus::InboundMessage&,
                                                    const kabot::bus::OutboundMessage&)>;

    AgentRegistry(kabot::bus::MessageBus& bus,
                  kabot::providers::LLMProvider& provider,
                  const kabot::config::Config& config,
                  kabot::cron::CronService* cron = nullptr);

    void Start();
    void Stop();
    void SetInboundExecutionReporter(InboundExecutionReporter reporter);
    void SetInboundInterceptor(InboundInterceptor interceptor);
    void SetInboundPostProcessor(InboundPostProcessor processor);

    kabot::bus::OutboundMessage HandleInbound(kabot::bus::InboundMessage msg);
    std::string ProcessDirect(const std::string& agent_name,
                              const std::string& content,
                              const std::string& session_key,
                              const DirectExecutionObserver& observer = {},
                              const DirectExecutionTarget& target = {},
                              const DirectOutboundObserver& outbound_observer = {});
    const kabot::config::AgentInstanceConfig* GetAgentConfig(const std::string& name) const;
    std::string ResolveAgentName(const kabot::bus::InboundMessage& msg) const;
    std::string DefaultAgentName() const;

private:
    void InitAgents();
    void RunLoop();

    kabot::bus::MessageBus& bus_;
    kabot::providers::LLMProvider& provider_;
    kabot::config::Config config_;
    kabot::cron::CronService* cron_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<AgentLoop>> agents_;
    InboundExecutionReporter inbound_reporter_;
    InboundInterceptor inbound_interceptor_;
    InboundPostProcessor inbound_post_processor_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace kabot::agent
