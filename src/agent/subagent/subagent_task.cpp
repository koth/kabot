#include "agent/subagent/subagent_task.hpp"

#include <chrono>

#include "utils/logging.hpp"

namespace kabot::subagent {

std::string SubagentTaskManager::RegisterTask(const std::string& agent_id,
                                               const std::string& description,
                                               const std::string& parent_session_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    int id = ++task_counter_;
    std::string task_id = "subagent_task_" + std::to_string(id);
    AgentTaskRecord task;
    task.task_id = task_id;
    task.agent_id = agent_id;
    task.parent_session_id = parent_session_id;
    task.description = description;
    task.status = SubagentStatus::kIdle;
    task.started_at = std::chrono::steady_clock::now();
    tasks_[task_id] = task;
    LOG_INFO("[subagent] registered task={} agent={} parent_session={}", task_id, agent_id, parent_session_id);
    return task_id;
}

AgentTaskRecord* SubagentTaskManager::GetTask(const std::string& task_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return &it->second;
    }
    return nullptr;
}

AgentTaskRecord* SubagentTaskManager::GetTaskByAgentId(const std::string& agent_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    for (auto& [tid, task] : tasks_) {
        if (task.agent_id == agent_id) {
            return &task;
        }
    }
    return nullptr;
}

bool SubagentTaskManager::UpdateStatus(const std::string& task_id, SubagentStatus status) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return false;
    it->second.status = status;
    if (IsTerminalStatus(status)) {
        it->second.finished_at = std::chrono::steady_clock::now();
    }
    return true;
}

bool SubagentTaskManager::UpdateProgress(const std::string& task_id,
                                          const AgentTaskRecord::Progress& progress) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return false;
    it->second.progress = progress;
    return true;
}

bool SubagentTaskManager::MarkCompleted(const std::string& task_id,
                                         const std::string& output,
                                         int total_tokens) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return false;
    it->second.status = SubagentStatus::kCompleted;
    it->second.finished_at = std::chrono::steady_clock::now();
    it->second.output_file = output;
    it->second.total_tokens = total_tokens;
    LOG_INFO("[subagent] task completed task={} total_tokens={}", task_id, total_tokens);
    return true;
}

bool SubagentTaskManager::MarkFailed(const std::string& task_id,
                                      const std::string& error_code,
                                      const std::string& error_message,
                                      bool retryable) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return false;
    it->second.status = SubagentStatus::kFailed;
    it->second.finished_at = std::chrono::steady_clock::now();
    it->second.error.code = error_code;
    it->second.error.message = error_message;
    it->second.error.retryable = retryable;
    LOG_WARN("[subagent] task failed task={} error={}", task_id, error_message);
    return true;
}

bool SubagentTaskManager::MarkAborted(const std::string& task_id) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = tasks_.find(task_id);
    if (it == tasks_.end()) return false;
    it->second.status = SubagentStatus::kAborted;
    it->second.finished_at = std::chrono::steady_clock::now();
    LOG_INFO("[subagent] task aborted task={}", task_id);
    return true;
}

std::vector<AgentTaskRecord> SubagentTaskManager::ListTasks() const {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<AgentTaskRecord> result;
    for (const auto& [_, task] : tasks_) {
        result.push_back(task);
    }
    return result;
}

std::vector<AgentTaskRecord> SubagentTaskManager::ListActiveTasks() const {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<AgentTaskRecord> result;
    for (const auto& [_, task] : tasks_) {
        if (!IsTerminalStatus(task.status)) {
            result.push_back(task);
        }
    }
    return result;
}

} // namespace kabot::subagent
