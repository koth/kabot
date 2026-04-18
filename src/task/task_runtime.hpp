#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "agent/agent_registry.hpp"
#include "agent/memory_store.hpp"
#include "bus/events.hpp"
#include "config/config_schema.hpp"
#include "cron/cron_service.hpp"
#include "relay/relay_manager.hpp"
#include "utils/thread_pool.hpp"

namespace kabot::task {

class TaskRuntime {
public:
    TaskRuntime(const kabot::config::Config& config,
                kabot::agent::AgentRegistry& agents,
                kabot::relay::RelayManager& relay,
                kabot::cron::CronService* cron = nullptr);
    ~TaskRuntime();

    void Start();
    void Stop();

    bool HandleInbound(kabot::bus::InboundMessage& msg,
                       kabot::bus::OutboundMessage& outbound);
    void ObserveInboundResult(const kabot::bus::InboundMessage& msg,
                              const kabot::bus::OutboundMessage& outbound);
    bool HandleCron(const kabot::cron::CronJob& job, std::string& response);
    std::string DumpStateJson() const;

private:
    struct DailySummaryRecord {
        std::string summary_date;
        std::string uploaded_at;
        std::string status;
        std::string message;
    };

    struct WaitingTask {
        std::string task_id;
        std::string local_agent;
        std::string session_key;
        std::string question;
        std::string channel;
        std::string channel_instance;
        std::string chat_id;
        std::string reply_to;
        std::string agent_name;
        std::string updated_at;
    };

    struct ActiveTask {
        std::string task_id;
        std::string session_key;
    };

    void AgentPollLoop(const std::string& local_agent);
    void ExecuteClaimedTask(const std::string& local_agent,
                            const kabot::relay::RelayTask& task);
    bool ResumeWaitingTask(const WaitingTask& waiting_task,
                           const std::string& user_reply,
                           std::string& final_result);
    bool HasPendingTaskForLocalAgent(const std::string& local_agent) const;
    bool IsTaskClaimed(const std::string& task_id) const;
    void MarkActiveTask(const std::string& local_agent,
                        const std::string& task_id,
                        const std::string& session_key);
    void ClearActiveTask(const std::string& local_agent);
    void EnsureDailySummaryJobs();
    void RemoveDailySummaryJobs();
    bool HandleDailySummaryCron(const kabot::cron::CronJob& job, std::string& response);
    bool TryResumeWaitingTask(const kabot::bus::InboundMessage& msg,
                              kabot::bus::OutboundMessage& outbound);
    void UpsertWaitingTask(const kabot::bus::InboundMessage& msg,
                           const kabot::bus::OutboundMessage& outbound);
    void ClearWaitingTask(const kabot::bus::InboundMessage& msg);
    bool ShouldWaitForUser(const kabot::bus::InboundMessage& msg,
                           const kabot::bus::OutboundMessage& outbound) const;
    std::string BuildExecutionLog(const kabot::session::Session& session) const;
    bool VerifyTaskCompletion(const std::string& local_agent,
                              const std::string& session_key,
                              const std::string& instruction,
                              const std::string& result,
                              const std::string& execution_log,
                              std::string& verification_reason);
    void LoadState();
    void SaveState() const;
    void SaveWaitingTasks() const;
    void SaveDailySummaries() const;
    void LoadWaitingTasks();
    void LoadDailySummaries();

    std::string SummaryJobId(const std::string& local_agent) const;
    std::string WaitingKey(const kabot::bus::InboundMessage& msg) const;
    std::filesystem::path WaitingTasksStatePath() const;
    std::filesystem::path DailySummaryStatePath() const;
    std::string TodayDate() const;
    std::string NowIso() const;

    kabot::config::Config config_;
    kabot::agent::AgentRegistry& agents_;
    kabot::relay::RelayManager& relay_;
    kabot::cron::CronService* cron_ = nullptr;
    std::atomic<bool> running_{false};
    std::vector<std::thread> poll_threads_;
    std::unique_ptr<kabot::ThreadPool> task_pool_;
    std::vector<std::string> cron_job_ids_;
    std::unordered_map<std::string, DailySummaryRecord> daily_summary_records_;
    std::unordered_map<std::string, WaitingTask> waiting_tasks_;
    std::unordered_map<std::string, ActiveTask> active_tasks_;
    std::unordered_set<std::string> claimed_task_ids_;
    mutable std::mutex mutex_;
};

}  // namespace kabot::task
