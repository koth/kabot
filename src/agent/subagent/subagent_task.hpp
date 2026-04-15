#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/subagent/subagent_types.hpp"

namespace kabot::subagent {

class SubagentTaskManager {
public:
    std::string RegisterTask(const std::string& agent_id,
                             const std::string& description,
                             const std::string& parent_session_id = "");
    AgentTaskRecord* GetTask(const std::string& task_id);
    AgentTaskRecord* GetTaskByAgentId(const std::string& agent_id);

    bool UpdateStatus(const std::string& task_id, SubagentStatus status);
    bool UpdateProgress(const std::string& task_id,
                        const AgentTaskRecord::Progress& progress);
    bool MarkCompleted(const std::string& task_id,
                       const std::string& output,
                       int total_tokens = 0);
    bool MarkFailed(const std::string& task_id,
                    const std::string& error_code,
                    const std::string& error_message,
                    bool retryable);
    bool MarkAborted(const std::string& task_id);

    std::vector<AgentTaskRecord> ListTasks() const;
    std::vector<AgentTaskRecord> ListActiveTasks() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, AgentTaskRecord> tasks_;
    std::atomic<int> task_counter_{0};
};

} // namespace kabot::subagent
