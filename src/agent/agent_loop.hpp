#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "agent/context_builder.hpp"
#include "agent/memory_store.hpp"
#include "agent/subagent/subagent_service.hpp"
#include "agent/tools/tool_registry.hpp"
#include "cron/cron_service.hpp"
#include "bus/message_bus.hpp"
#include "config/config_schema.hpp"
#include "providers/llm_provider.hpp"
#include "session/session_manager.hpp"
#include "utils/cancel_token.hpp"

namespace kabot::relay {
class RelayManager;
}

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
        kabot::config::TaskSystemConfig task_system = {},
        kabot::cron::CronService* cron = nullptr);
    void Run();
    void Stop();
    void SetRelayManager(kabot::relay::RelayManager* relay_manager);
    kabot::bus::OutboundMessage HandleInbound(const kabot::bus::InboundMessage& msg,
                                              const DirectExecutionObserver& observer = {},
                                              const std::function<void(bool, const std::string&)>& completion = {});
    std::string ProcessDirect(const std::string& content,
                              const std::string& session_key,
                              const DirectExecutionObserver& observer = {},
                              const DirectExecutionTarget& target = {},
                              const DirectOutboundObserver& outbound_observer = {},
                              const kabot::CancelToken& cancel_token = {});
    std::vector<std::string> RegisteredTools() const;
    std::string SpawnSubagent(const kabot::subagent::AgentSpawnInput& input,
                              const std::string& session_key = "");
    kabot::session::Session GetSession(const std::string& session_key);

private:
    kabot::bus::MessageBus& bus_;
    kabot::providers::LLMProvider& provider_;
    std::string workspace_;
    kabot::config::AgentDefaults config_;
    kabot::config::QmdConfig qmd_;
    kabot::config::TaskSystemConfig task_system_;
    kabot::agent::ContextBuilder context_;
    kabot::session::SessionManager sessions_;
    kabot::agent::MemoryStore memory_;
    kabot::agent::tools::ToolRegistry tools_;
    kabot::cron::CronService* cron_ = nullptr;
    kabot::relay::RelayManager* relay_manager_ = nullptr;
    std::unique_ptr<kabot::subagent::SubagentService> subagent_service_;
    bool running_ = false;
    std::mutex process_mutex_;

    kabot::bus::OutboundMessage ProcessMessage(const kabot::bus::InboundMessage& msg,
                                               const DirectExecutionObserver& observer = {});
    kabot::bus::OutboundMessage ProcessSystemMessage(const kabot::bus::InboundMessage& msg);
    std::string ExecuteToolWithGuardrails(kabot::session::Session& session,
                                          const kabot::providers::ToolCallRequest& call);
    void RegisterDefaultTools();
    void AppendMemoryEntry(const std::string& session_key,
                           const std::string& memory_block);
    void UpdateQmdIndex() const;
};

}  // namespace kabot::agent
