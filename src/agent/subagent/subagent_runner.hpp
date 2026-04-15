#pragma once

#include <future>
#include <string>

#include "agent/subagent/subagent_types.hpp"
#include "agent/subagent/subagent_task.hpp"
#include "agent/subagent/subagent_transcript.hpp"
#include "agent/tools/tool_registry.hpp"
#include "providers/llm_provider.hpp"
#include "config/config_schema.hpp"

namespace kabot::subagent {

class SubagentRunner {
public:
    SubagentRunner(kabot::providers::LLMProvider& provider,
                   kabot::agent::tools::ToolRegistry& tools,
                   SubagentTaskManager& task_manager,
                   SubagentTranscriptStore& transcript_store,
                   std::string workspace,
                   kabot::config::AgentDefaults defaults);
    
    SubagentRunSummary RunSync(const RunAgentParams& params,
                               const SubagentMessageHandler& on_message = {},
                               const std::string& task_id = {});

    std::string RunAsync(const RunAgentParams& params);

    std::string Resume(const std::string& agent_id,
                       const SubagentMessageHandler& on_message = {});

    void SetTaskCompletionHandler(std::function<void(const AgentTaskRecord&)> handler) {
        task_completion_handler_ = std::move(handler);
    }

private:
    kabot::providers::LLMProvider& provider_;
    kabot::agent::tools::ToolRegistry& tools_;
    SubagentTaskManager& task_manager_;
    SubagentTranscriptStore& transcript_store_;
    std::string workspace_;
    kabot::config::AgentDefaults defaults_;
    std::function<void(const AgentTaskRecord&)> task_completion_handler_;

    SubagentRunSummary DoRun(const RunAgentParams& params,
                             const SubagentMessageHandler& on_message,
                             const std::string& task_id);
    void DoRunAsync(const RunAgentParams& params,
                    const std::string& task_id);
};

} // namespace kabot::subagent
