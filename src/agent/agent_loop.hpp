#pragma once

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
    std::string ProcessDirect(const std::string& content, const std::string& session_key);

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

    kabot::bus::OutboundMessage ProcessMessage(const kabot::bus::InboundMessage& msg);
    kabot::bus::OutboundMessage ProcessSystemMessage(const kabot::bus::InboundMessage& msg);
    void RegisterDefaultTools();
    void AppendMemoryEntry(const std::string& session_key,
                           const std::string& memory_block);
    void UpdateQmdIndex() const;
};

}  // namespace kabot::agent
