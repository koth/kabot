#pragma once

#include <memory>
#include <string>
#include <vector>

#include "agent/subagent/subagent_runner.hpp"
#include "agent/subagent/subagent_task.hpp"
#include "agent/subagent/subagent_transcript.hpp"
#include "agent/tools/tool_registry.hpp"
#include "providers/llm_provider.hpp"
#include "config/config_schema.hpp"

namespace kabot::subagent {

class SubagentService {
public:
    SubagentService(kabot::providers::LLMProvider& provider,
                    kabot::agent::tools::ToolRegistry& tools,
                    std::string workspace,
                    kabot::config::AgentDefaults defaults);
    
    struct SpawnResult {
        std::string type;
        std::string task_id;
        std::string agent_id;
        std::string result;
    };
    
    SpawnResult Spawn(const AgentSpawnInput& input,
                      const SubagentContext& parent_ctx);
    
    std::string Resume(const std::string& agent_id,
                       const SubagentMessageHandler& on_message = {});
    
    std::vector<AgentTaskRecord> ListTasks() const;
    
    SubagentRunner& Runner() { return *runner_; }
    SubagentTaskManager& TaskManager() { return task_manager_; }
    SubagentTranscriptStore& TranscriptStore() { return transcript_store_; }
    
private:
    kabot::providers::LLMProvider& provider_;
    kabot::agent::tools::ToolRegistry& tools_;
    SubagentTaskManager task_manager_;
    SubagentTranscriptStore transcript_store_;
    std::unique_ptr<SubagentRunner> runner_;
};

} // namespace kabot::subagent
