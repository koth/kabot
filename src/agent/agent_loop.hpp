#pragma once

#include <functional>
#include <mutex>
#include <string>

#include "agent/context_builder.hpp"
#include "agent/memory_store.hpp"
#include "agent/tools/tool_registry.hpp"
#include "cron/cron_service.hpp"
#include "bus/message_bus.hpp"
#include "config/config_schema.hpp"
#include "providers/llm_provider.hpp"
#include "session/session_manager.hpp"

namespace kabot::agent {

enum class DirectExecutionPhase {
    kReceived,
    kProcessing,
    kCallingTools,
    kReplying,
};

using DirectExecutionObserver = std::function<void(DirectExecutionPhase)>;
using DirectOutboundObserver = std::function<void(const kabot::bus::OutboundMessage&)>;

struct DirectExecutionTarget {
    std::string channel;
    std::string channel_instance;
    std::string chat_id;
};

std::string DirectExecutionPhaseSummary(DirectExecutionPhase phase);

class AgentLoop {
public:
    AgentLoop(
        kabot::bus::MessageBus& bus,
        kabot::providers::LLMProvider& provider,
        std::string workspace,
        kabot::config::AgentDefaults config,
        kabot::config::QmdConfig qmd,
        kabot::cron::CronService* cron = nullptr);
    void Run();
    void Stop();
    kabot::bus::OutboundMessage HandleInbound(const kabot::bus::InboundMessage& msg,
                                              const DirectExecutionObserver& observer = {},
                                              const std::function<void(bool, const std::string&)>& completion = {});
    std::string ProcessDirect(const std::string& content,
                              const std::string& session_key,
                              const DirectExecutionObserver& observer = {},
                              const DirectExecutionTarget& target = {},
                              const DirectOutboundObserver& outbound_observer = {});
    std::vector<std::string> RegisteredTools() const;

private:
    kabot::bus::MessageBus& bus_;
    kabot::providers::LLMProvider& provider_;
    std::string workspace_;
    kabot::config::AgentDefaults config_;
    kabot::config::QmdConfig qmd_;
    kabot::agent::ContextBuilder context_;
    kabot::session::SessionManager sessions_;
    kabot::agent::MemoryStore memory_;
    kabot::agent::tools::ToolRegistry tools_;
    kabot::cron::CronService* cron_ = nullptr;
    bool running_ = false;
    std::mutex process_mutex_;

    kabot::bus::OutboundMessage ProcessMessage(const kabot::bus::InboundMessage& msg,
                                               const DirectExecutionObserver& observer = {});
    kabot::bus::OutboundMessage ProcessSystemMessage(const kabot::bus::InboundMessage& msg);
    void RegisterDefaultTools();
    void AppendMemoryEntry(const std::string& session_key,
                           const std::string& memory_block);
    void UpdateQmdIndex() const;
};

}  // namespace kabot::agent
