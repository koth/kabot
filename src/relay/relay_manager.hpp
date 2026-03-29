#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "agent/agent_registry.hpp"
#include "config/config_schema.hpp"

namespace kabot::relay {

struct RelayTaskInteraction {
    std::string channel;
    std::string channel_instance;
    std::string chat_id;
    std::string reply_to;
};

struct RelayTaskProject {
    std::string project_id;
    std::string name;
    std::string description;
    std::unordered_map<std::string, std::string> metadata;
};

struct RelayTask {
    std::string task_id;
    std::string title;
    std::string instruction;
    std::string session_key;
    std::string created_at;
    std::string priority;
    RelayTaskInteraction interaction;
    std::unordered_map<std::string, std::string> metadata;
    // Project context
    RelayTaskProject project;
    // Dependency context
    std::vector<std::string> depends_on_task_ids;
    std::unordered_map<std::string, std::string> dependency_state;
    std::vector<std::string> blocked_by_task_ids;
    // Review/merge context
    std::string merge_request;
    // Waiting state
    bool waiting_user = false;
};

struct RelayTaskClaimResult {
    bool success = false;
    bool found = false;
    int http_status = 0;
    std::string message;
    RelayTask task;
};

struct RelayTaskStatusUpdate {
    std::string status;
    std::string message;
    int progress = -1;
    std::string reported_at;
    std::string session_key;
    std::string result;
    std::string error;
    RelayTaskInteraction waiting_user;
};

struct RelayTaskStatusUpdateResult {
    bool success = false;
    int http_status = 0;
    std::string message;
};

struct DailySummaryUploadResult {
    bool success = false;
    int http_status = 0;
    std::string message;
};

class RelayManager {
public:
    RelayManager(const kabot::config::Config& config,
                 kabot::agent::AgentRegistry& agents);
    ~RelayManager();

    void Start();
    void Stop();
    std::vector<std::string> ManagedLocalAgents() const;
    bool HasManagedLocalAgent(const std::string& local_agent) const;
    RelayTaskClaimResult ClaimNextTask(const std::string& local_agent,
                                       bool supports_interaction = true);
    RelayTaskStatusUpdateResult UpdateTaskStatus(const std::string& local_agent,
                                                 const std::string& task_id,
                                                 const RelayTaskStatusUpdate& update);
    DailySummaryUploadResult UploadDailySummary(const std::string& local_agent,
                                                const std::string& summary_date,
                                                const std::string& content,
                                                const std::string& reported_at = {});

private:
    class Worker;

    kabot::config::Config config_;
    kabot::agent::AgentRegistry& agents_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::unordered_map<std::string, Worker*> workers_by_local_agent_;
    std::atomic<bool> running_{false};
};

}  // namespace kabot::relay
