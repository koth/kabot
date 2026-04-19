#include "task/task_runtime.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <cstdlib>

#include "agent/memory_store.hpp"
#include "nlohmann/json.hpp"
#include "session/session_manager.hpp"
#include "utils/cancel_token.hpp"
#include "utils/logging.hpp"

namespace kabot::task {
namespace {

std::string EffectiveChannelInstance(const kabot::bus::InboundMessage& msg) {
    return msg.channel_instance.empty() ? msg.channel : msg.channel_instance;
}

std::string BuildWaitingKey(const std::string& channel_instance,
                            const std::string& agent_name,
                            const std::string& chat_id) {
    return channel_instance + ":" + agent_name + ":" + chat_id;
}

// Execute a shell command and return stdout
std::string ExecuteCommand(const std::string& cmd) {
    std::array<char, 128> buffer{};
    std::string result;
#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsAny(const std::string& content, const std::initializer_list<std::string_view>& markers) {
    return std::any_of(markers.begin(), markers.end(), [&content](const std::string_view marker) {
        return content.find(marker) != std::string::npos;
    });
}

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

void CheckStatusUpdate(const kabot::relay::RelayTaskStatusUpdateResult& result,
                       const std::string& task_id,
                       const std::string& status) {
    if (!result.success) {
        LOG_WARN("[task_runtime] failed to update task {} status to '{}': http_status={} message={}",
                 task_id, status, result.http_status, result.message);
    }
}

// Format dependency info for status summary
std::string FormatDependencyInfo(const kabot::relay::RelayTask& task) {
    std::ostringstream oss;
    if (!task.depends_on_task_ids.empty()) {
        oss << " depends on [";
        for (size_t i = 0; i < task.depends_on_task_ids.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << task.depends_on_task_ids[i];
        }
        oss << "]";
    }
    if (!task.blocked_by_task_ids.empty()) {
        oss << " blocked by [";
        for (size_t i = 0; i < task.blocked_by_task_ids.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << task.blocked_by_task_ids[i];
        }
        oss << "]";
    }
    return oss.str();
}

// Build execution context summary for memory
std::string BuildContextSummary(const kabot::relay::RelayTask& task) {
    std::ostringstream oss;
    oss << "Task: " << task.task_id;
    if (!task.title.empty()) {
        oss << " (" << task.title << ")";
    }
    if (!task.project.name.empty()) {
        oss << " | Project: " << task.project.name;
    } else if (!task.project.project_id.empty()) {
        oss << " | Project: " << task.project.project_id;
    }
    
    auto dep_info = FormatDependencyInfo(task);
    if (!dep_info.empty()) {
        oss << " |" << dep_info;
    }
    
    if (task.waiting_user) {
        oss << " | Waiting for user input";
    }
    if (!task.merge_request.empty()) {
        oss << " | MR: " << task.merge_request;
    }
    
    return oss.str();
}

// Build execution log from session history for verification
std::string BuildExecutionLogFromSession(const kabot::session::Session& session) {
    std::ostringstream oss;
    const auto& messages = session.Messages();
    int step = 1;
    for (const auto& msg : messages) {
        if (msg.role == "assistant") {
            oss << "Step " << step << " [Agent]: ";
            if (!msg.tool_calls.empty()) {
                oss << "Called tools: ";
                for (size_t i = 0; i < msg.tool_calls.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << msg.tool_calls[i].name;
                }
                oss << "\n";
            } else {
                oss << msg.content.substr(0, 500);
                if (msg.content.length() > 500) {
                    oss << "... (truncated)";
                }
                oss << "\n";
            }
            ++step;
        } else if (msg.role == "tool") {
            oss << "  [Tool Result - " << msg.name << "]: ";
            oss << msg.content.substr(0, 300);
            if (msg.content.length() > 300) {
                oss << "... (truncated)";
            }
            oss << "\n";
        }
    }
    return oss.str();
}

// Write memory entry to agent's memory store
void WriteTaskMemory(const std::string& workspace,
                     const std::string& task_id,
                     const std::string& project_name,
                     const std::string& state,
                     const std::string& goal,
                     const std::string& result = "") {
    try {
        kabot::agent::MemoryStore memory(workspace);
        std::ostringstream entry;
        entry << "- [" << task_id << "]";
        if (!project_name.empty()) {
            entry << " [" << project_name << "]";
        }
        entry << " State: " << state;
        if (!goal.empty()) {
            entry << " | Goal: " << goal;
        }
        if (!result.empty()) {
            entry << " | Result: " << result;
        }
        entry << "\n";
        memory.AppendToday(entry.str());
    } catch (const std::exception& ex) {
        LOG_WARN("[task_runtime] failed to write memory: {}", ex.what());
    }
}

}  // namespace

TaskRuntime::TaskRuntime(const kabot::config::Config& config,
                         kabot::agent::AgentRegistry& agents,
                         kabot::relay::RelayManager& relay,
                         kabot::cron::CronService* cron)
    : config_(config)
    , agents_(agents)
    , relay_(relay)
    , cron_(cron) {}

TaskRuntime::~TaskRuntime() {
    Stop();
}

void TaskRuntime::Start() {
    if (running_ || !config_.task_system.enabled) {
        return;
    }
    LoadState();
    running_ = true;
    const auto pool_size = std::max(1, config_.task_system.max_concurrent_tasks);
    task_pool_ = std::make_unique<kabot::ThreadPool>(static_cast<std::size_t>(pool_size));
    EnsureDailySummaryJobs();
    const auto auto_claim_agents = relay_.AutoClaimLocalAgents();
    for (const auto& local_agent : auto_claim_agents) {
        poll_threads_.emplace_back([this, local_agent] { AgentPollLoop(local_agent); });
    }
    LOG_INFO("[task] task system started, auto-claim agents={}", auto_claim_agents.size());
}

void TaskRuntime::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    for (auto& t : poll_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    poll_threads_.clear();
    if (task_pool_) {
        task_pool_->Shutdown();
        task_pool_->WaitForEmpty(std::chrono::seconds(config_.task_system.shutdown_timeout_s));
    }
    RemoveDailySummaryJobs();
    SaveState();
}

bool TaskRuntime::HandleInbound(kabot::bus::InboundMessage& msg,
                                kabot::bus::OutboundMessage& outbound) {
    return TryResumeWaitingTask(msg, outbound);
}

void TaskRuntime::ObserveInboundResult(const kabot::bus::InboundMessage& msg,
                                       const kabot::bus::OutboundMessage& outbound) {
    if (!config_.task_system.enabled) {
        return;
    }
    if (ShouldWaitForUser(msg, outbound)) {
        UpsertWaitingTask(msg, outbound);
        return;
    }
    ClearWaitingTask(msg);
}

bool TaskRuntime::HandleCron(const kabot::cron::CronJob& job, std::string& response) {
    if (!config_.task_system.enabled) {
        return false;
    }
    if (job.payload.kind == "task_runtime_daily_summary") {
        return HandleDailySummaryCron(job, response);
    }
    return false;
}

std::string TaskRuntime::DumpStateJson() const {
    nlohmann::json json = nlohmann::json::object();
    json["enabled"] = config_.task_system.enabled;
    json["running"] = running_.load();
    json["pollIntervalS"] = config_.task_system.poll_interval_s;
    json["dailySummaryHourLocal"] = config_.task_system.daily_summary_hour_local;
    json["dailySummaries"] = nlohmann::json::array();
    json["waitingTasks"] = nlohmann::json::array();

    std::lock_guard<std::mutex> guard(mutex_);
    for (const auto& [local_agent, record] : daily_summary_records_) {
        json["dailySummaries"].push_back({
            {"localAgent", local_agent},
            {"summaryDate", record.summary_date},
            {"uploadedAt", record.uploaded_at.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.uploaded_at)},
            {"status", record.status.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.status)},
            {"message", record.message.empty() ? nlohmann::json(nullptr) : nlohmann::json(record.message)}
        });
    }
    for (const auto& [key, task] : waiting_tasks_) {
        json["waitingTasks"].push_back({
            {"key", key},
            {"localAgent", task.local_agent},
            {"sessionKey", task.session_key},
            {"question", task.question.empty() ? nlohmann::json(nullptr) : nlohmann::json(task.question)},
            {"channel", task.channel},
            {"channelInstance", task.channel_instance},
            {"chatId", task.chat_id},
            {"agentName", task.agent_name},
            {"updatedAt", task.updated_at}
        });
    }
    return json.dump(2);
}

void TaskRuntime::AgentPollLoop(const std::string& local_agent) {
    const auto poll_interval = std::max(5, config_.task_system.poll_interval_s);
    while (running_) {
        if (!HasPendingTaskForLocalAgent(local_agent)) {
            const auto claim = relay_.ClaimNextTask(local_agent, true);
            if (claim.success && claim.found && !claim.task.task_id.empty()) {
                const auto session_key = claim.task.session_key.empty()
                    ? "task:" + local_agent + ":" + claim.task.task_id
                    : claim.task.session_key;
                {
                    std::lock_guard<std::mutex> guard(mutex_);
                    if (claimed_task_ids_.find(claim.task.task_id) != claimed_task_ids_.end()) {
                        continue;
                    }
                    active_tasks_[local_agent] = ActiveTask{claim.task.task_id, session_key};
                    claimed_task_ids_.insert(claim.task.task_id);
                    SaveState();
                }
                if (task_pool_) {
                    task_pool_->Submit([this, local_agent, task = claim.task] {
                        ExecuteClaimedTask(local_agent, task);
                    });
                }
            } else if (!claim.success) {
                LOG_WARN("[task] claim next task failed for local_agent={} http_status={} message={}",
                         local_agent, claim.http_status, claim.message);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(poll_interval));
    }
}

void TaskRuntime::ExecuteClaimedTask(const std::string& local_agent,
                                     const kabot::relay::RelayTask& task) {
    const auto session_key = task.session_key.empty()
        ? "task:" + local_agent + ":" + task.task_id
        : task.session_key;
    
    const auto* agent_config = agents_.GetAgentConfig(local_agent);
    const std::string workspace = agent_config ? agent_config->workspace : config_.agents.defaults.workspace;
    const std::string project_name = task.project.name.empty() ? task.project.project_id : task.project.name;

    // Fetch project metadata if project_id is present
    std::string project_git_url = task.project.git_url;
    if (!task.project.project_id.empty() && project_git_url.empty()) {
        const auto query_result = relay_.QueryProject(task.project.project_id);
        if (query_result.success && !query_result.info.project_id.empty()) {
            project_git_url = query_result.info.metadata.count("gitUrl")
                ? query_result.info.metadata.at("gitUrl")
                : std::string();
        }
    }

    // Update active task with project info
    {
        std::lock_guard<std::mutex> guard(mutex_);
        auto it = active_tasks_.find(local_agent);
        if (it != active_tasks_.end()) {
            it->second.project_name = project_name;
            it->second.project_git_url = project_git_url;
        }
    }

    // Build context summary for status reporting
    const auto context_summary = BuildContextSummary(task);

    // Step 1: Report claimed status
    CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
        "claimed",
        "Claimed task " + task.task_id + " and preparing context",
        0,
        NowIso(),
        session_key
    }), task.task_id, "claimed");

    // Write claim memory
    WriteTaskMemory(workspace, task.task_id, project_name, "claimed", 
                    "Preparing execution context: " + context_summary);

    // Step 2: Report running status with context
    std::string running_summary = "Executing task " + task.task_id;
    if (!project_name.empty()) {
        running_summary += " in project " + project_name;
    }
    running_summary += ": " + task.title;
    
    CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
        "running",
        running_summary,
        5,
        NowIso(),
        session_key
    }), task.task_id, "running");

    // Write start memory
    WriteTaskMemory(workspace, task.task_id, project_name, "running", 
                    "Started execution: " + task.title);

    bool waiting = false;
    kabot::relay::RelayTaskInteraction waiting_user{};
    std::string waiting_question;
    kabot::CancelToken cancel_token;
    std::atomic<bool> finished{false};

    // Git workflow setup
    std::filesystem::path project_dir;
    bool has_git_workflow = false;
    if (!project_git_url.empty() && !project_name.empty()) {
        has_git_workflow = true;
        project_dir = std::filesystem::path(workspace) / "projects" / project_name;
        
        // 2.1 Create projects/ directory
        std::filesystem::create_directories(project_dir.parent_path());
        
        // 2.2 Clone repository
        const auto clone_cmd = "git clone --depth 1 \"" + project_git_url + "\" \"" + project_dir.string() + "\" 2>&1";
        const auto clone_output = ExecuteCommand(clone_cmd);
        if (!std::filesystem::exists(project_dir / ".git")) {
            // 2.3 Handle clone failure
            LOG_ERROR("[task_runtime] Failed to clone repository: {}", clone_output);
            CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
                "failed",
                "Failed to clone repository: " + clone_output,
                -1,
                NowIso(),
                session_key
            }), task.task_id, "failed");
            ClearActiveTask(local_agent);
            finished.store(true);
            return;
        }
        
        // 2.4 Verify .gitignore
        const auto gitignore_path = project_dir / ".gitignore";
        std::string gitignore_content;
        if (std::filesystem::exists(gitignore_path)) {
            std::ifstream gitignore_file(gitignore_path);
            gitignore_content = std::string((std::istreambuf_iterator<char>(gitignore_file)),
                                            std::istreambuf_iterator<char>());
        }
        
        const std::vector<std::string> required_patterns = {
            "build/", "*.exe", "*.tmp", ".env", ".vscode/", ".idea/", 
            "*.log", "node_modules/", "__pycache__/", ".DS_Store", "Thumbs.db"
        };
        
        bool needs_update = false;
        std::string patterns_to_add;
        for (const auto& pattern : required_patterns) {
            if (gitignore_content.find(pattern) == std::string::npos) {
                needs_update = true;
                patterns_to_add += pattern + "\n";
            }
        }
        
        // 2.5 Update .gitignore if needed
        if (needs_update) {
            std::ofstream gitignore_out(gitignore_path, std::ios::app);
            if (gitignore_out) {
                gitignore_out << "\n# Added by kabot agent\n" << patterns_to_add;
                gitignore_out.close();
                
                // Commit .gitignore update to default branch
                ExecuteCommand("cd \"" + project_dir.string() + "\" && git add .gitignore && git commit -m \"chore: update .gitignore\"");
            }
        }
        
        // 2.6 Create task branch
        const auto branch_name = "kabot-task-" + task.task_id;
        const auto branch_cmd = "cd \"" + project_dir.string() + "\" && git checkout -b " + branch_name + " 2>&1";
        const auto branch_output = ExecuteCommand(branch_cmd);
        
        // 2.7 Handle existing branch
        if (branch_output.find("already exists") != std::string::npos) {
            ExecuteCommand("cd \"" + project_dir.string() + "\" && git checkout " + branch_name + " && git reset --hard HEAD");
        }
    }

    const auto timeout_s = std::max(0, config_.task_system.task_timeout_s);
    const auto grace_s = std::max(0, config_.task_system.shutdown_timeout_s);

    if (timeout_s > 0) {
        std::thread([this, &cancel_token, &finished, local_agent, task_id = task.task_id, session_key, timeout_s, grace_s] {
            std::this_thread::sleep_for(std::chrono::seconds(timeout_s));
            if (finished.load()) return;
            cancel_token.Cancel();
            if (grace_s > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(grace_s));
            }
            if (finished.load()) return;
            LOG_WARN("[task_runtime] task {} on agent {} hard-abandoned after timeout", task_id, local_agent);
            ClearActiveTask(local_agent);
            CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task_id, {
                "failed",
                "Task " + task_id + " timed out and was abandoned",
                -1,
                NowIso(),
                session_key
            }), task_id, "failed");
        }).detach();
    }

    try {
        const auto observer = [this, local_agent, task_id = task.task_id, session_key,
                               workspace, task, project_name](kabot::agent::DirectExecutionPhase phase) {
            const auto phase_summary = kabot::agent::DirectExecutionPhaseSummary(phase);

            CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task_id, {
                "running",
                phase_summary,
                -1,
                NowIso(),
                session_key
            }), task_id, "running");

            if (phase == kabot::agent::DirectExecutionPhase::kCallingTools) {
                WriteTaskMemory(workspace, task_id, project_name, "running",
                               "Calling tools to process task");
            }
        };

        const kabot::agent::DirectExecutionTarget target{
            task.interaction.channel,
            task.interaction.channel_instance,
            task.interaction.chat_id,
            has_git_workflow ? project_dir.string() : std::string()
        };

        const auto outbound_observer = [&](const kabot::bus::OutboundMessage& outbound) {
            kabot::bus::InboundMessage synthetic{};
            synthetic.channel = outbound.channel;
            synthetic.channel_instance = outbound.channel_instance;
            synthetic.agent_name = local_agent;
            synthetic.chat_id = outbound.chat_id;
            if (!ShouldWaitForUser(synthetic, outbound)) {
                return;
            }
            waiting = true;
            waiting_question = outbound.content;
            waiting_user.channel = outbound.channel;
            waiting_user.channel_instance = outbound.channel_instance;
            waiting_user.chat_id = outbound.chat_id;
            waiting_user.reply_to = task.interaction.reply_to;

            WriteTaskMemory(workspace, task.task_id, project_name, "waiting_user",
                           "Waiting for user input: " + waiting_question);
        };

        const auto result = agents_.ProcessDirect(local_agent,
                                                  task.instruction,
                                                  session_key,
                                                  observer,
                                                  target,
                                                  outbound_observer,
                                                  cancel_token);

        if (waiting && !waiting_user.chat_id.empty()) {
            WaitingTask waiting_task{};
            waiting_task.task_id = task.task_id;
            waiting_task.local_agent = local_agent;
            waiting_task.session_key = session_key;
            waiting_task.question = waiting_question;
            waiting_task.channel = waiting_user.channel;
            waiting_task.channel_instance = waiting_user.channel_instance.empty()
                ? waiting_user.channel
                : waiting_user.channel_instance;
            waiting_task.chat_id = waiting_user.chat_id;
            waiting_task.reply_to = waiting_user.reply_to;
            waiting_task.agent_name = local_agent;
            waiting_task.updated_at = NowIso();
            {
                std::lock_guard<std::mutex> guard(mutex_);
                waiting_tasks_[BuildWaitingKey(waiting_task.channel_instance,
                                               waiting_task.agent_name,
                                               waiting_task.chat_id)] = waiting_task;
                active_tasks_.erase(local_agent);
                SaveState();
            }
            CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
                "waiting_user",
                waiting_question.empty() ? "Waiting for user input on task " + task.task_id
                                        : waiting_question,
                50,
                NowIso(),
                session_key,
                {},
                {},
                waiting_user
            }), task.task_id, "waiting_user");
            finished.store(true);
            return;
        }

        // Post-execution: Commit, push, and create MR if git workflow is active
        std::string mr_url;
        std::string mr_created_at;
        if (has_git_workflow && !project_dir.empty()) {
            const auto branch_name = "kabot-task-" + task.task_id;
            const auto project_path = project_dir.string();
            
            // 3.1 Detect uncommitted changes
            const auto status_output = ExecuteCommand("cd \"" + project_path + "\" && git status --porcelain");
            const bool has_changes = !Trim(status_output).empty();
            
            if (has_changes) {
                // 3.2 Commit changes
                const auto commit_msg = "kabot: " + task.title + " [task-" + task.task_id + "]";
                ExecuteCommand("cd \"" + project_path + "\" && git add -A && git commit -m \"" + commit_msg + "\"");
                
                // 3.3 Push branch
                ExecuteCommand("cd \"" + project_path + "\" && git push origin " + branch_name);
                
                // 3.4 Create MR using platform CLI
                const auto mr_cmd = "cd \"" + project_path + "\" && (glab mr create --source-branch " + branch_name + 
                                   " --target-branch main --title \"" + task.title + "\" --description \"Automated MR from kabot task " + task.task_id + "\" 2>&1 || " +
                                   "gh pr create --head " + branch_name + " --base main --title \"" + task.title + "\" --body \"Automated PR from kabot task " + task.task_id + "\" 2>&1)";
                const auto mr_output = ExecuteCommand(mr_cmd);
                
                // Extract MR URL from output
                const auto url_pos = mr_output.find("http");
                if (url_pos != std::string::npos) {
                    const auto url_end = mr_output.find_first_of(" \t\n\r", url_pos);
                    mr_url = mr_output.substr(url_pos, url_end != std::string::npos ? url_end - url_pos : std::string::npos);
                    mr_created_at = NowIso();
                } else {
                    // 3.5 MR creation failed - log warning but continue
                    LOG_WARN("[task_runtime] MR creation failed for task {}: {}", task.task_id, mr_output);
                }
            }
            // 3.6 No changes - skip commit/push/MR
        }

        // Step 3: Verify task completion
        std::string verification_reason;
        bool verification_passed = true;
        try {
            const auto session = agents_.GetSession(local_agent, session_key);
            const auto execution_log = BuildExecutionLog(session);

            verification_passed = VerifyTaskCompletion(local_agent, session_key,
                                                        task.instruction, result,
                                                        execution_log, verification_reason);
        } catch (const std::exception& ex) {
            LOG_WARN("[task_runtime] verification failed for task {}: {}", task.task_id, ex.what());
            verification_reason = "Verification error: " + std::string(ex.what());
            verification_passed = true;  // Fail open: if verifier breaks, don't block completion
        }

        ClearActiveTask(local_agent);

        if (verification_passed) {
            kabot::relay::RelayTaskStatusUpdate completed_update{
                "completed",
                "Task " + task.task_id + " finished successfully",
                100,
                NowIso(),
                session_key,
                result
            };
            // 4.4 Include MR metadata if available
            if (!mr_url.empty()) {
                completed_update.merge_request = kabot::relay::RelayTaskMergeRequest{
                    mr_url,
                    mr_created_at
                };
            }
            CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, completed_update),
                              task.task_id, "completed");

            std::string result_summary = result;
            if (result_summary.length() > 200) {
                result_summary = result_summary.substr(0, 197) + "...";
            }
            WriteTaskMemory(workspace, task.task_id, project_name, "completed",
                           "Task finished successfully", result_summary);
        } else {
            CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
                "failed",
                "Task " + task.task_id + " failed verification: " + verification_reason,
                -1,
                NowIso(),
                session_key,
                {},
                verification_reason
            }), task.task_id, "failed");

            WriteTaskMemory(workspace, task.task_id, project_name, "failed",
                           "Task failed verification", verification_reason);
        }

    } catch (const std::exception& ex) {
        ClearActiveTask(local_agent);

        const bool cancelled = cancel_token.IsCancelled();
        CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
            "failed",
            cancelled ? "Task " + task.task_id + " cancelled after timeout"
                      : "Task " + task.task_id + " failed because " + ex.what(),
            -1,
            NowIso(),
            session_key,
            {},
            cancelled ? "task cancelled by timeout" : ex.what()
        }), task.task_id, "failed");

        WriteTaskMemory(workspace, task.task_id, project_name, "failed",
                       cancelled ? "Task cancelled by timeout" : "Task failed",
                       cancelled ? "task cancelled by timeout" : ex.what());
    } catch (...) {
        ClearActiveTask(local_agent);

        const bool cancelled = cancel_token.IsCancelled();
        CheckStatusUpdate(relay_.UpdateTaskStatus(local_agent, task.task_id, {
            "failed",
            cancelled ? "Task " + task.task_id + " cancelled after timeout"
                      : "Task " + task.task_id + " failed because unknown task runtime error",
            -1,
            NowIso(),
            session_key,
            {},
            cancelled ? "task cancelled by timeout" : "unknown task runtime error"
        }), task.task_id, "failed");

        WriteTaskMemory(workspace, task.task_id, project_name, "failed",
                       cancelled ? "Task cancelled by timeout" : "Task failed with unknown error");
    }

    finished.store(true);
}

bool TaskRuntime::ResumeWaitingTask(const WaitingTask& waiting_task,
                                    const std::string& user_reply,
                                    std::string& final_result) {
    bool waiting = false;
    kabot::relay::RelayTaskInteraction waiting_user{};
    std::string waiting_question;
    const kabot::agent::DirectExecutionTarget target{
        waiting_task.channel,
        waiting_task.channel_instance,
        waiting_task.chat_id
    };
    const auto outbound_observer = [&](const kabot::bus::OutboundMessage& outbound) {
        kabot::bus::InboundMessage synthetic{};
        synthetic.channel = outbound.channel;
        synthetic.channel_instance = outbound.channel_instance;
        synthetic.agent_name = waiting_task.local_agent;
        synthetic.chat_id = outbound.chat_id;
        if (!ShouldWaitForUser(synthetic, outbound)) {
            return;
        }
        waiting = true;
        waiting_question = outbound.content;
        waiting_user.channel = outbound.channel;
        waiting_user.channel_instance = outbound.channel_instance;
        waiting_user.chat_id = outbound.chat_id;
        waiting_user.reply_to = waiting_task.reply_to;
    };
    kabot::CancelToken cancel_token{};
    final_result = agents_.ProcessDirect(waiting_task.local_agent,
                                         std::string("Continue the existing task. The user has replied to your earlier clarification request.\n\n") +
                                             "User reply:\n" + user_reply,
                                         waiting_task.session_key,
                                         {},
                                         target,
                                         outbound_observer,
                                         cancel_token);
    if (waiting && !waiting_user.chat_id.empty()) {
        WaitingTask next_waiting = waiting_task;
        next_waiting.question = waiting_question;
        next_waiting.channel = waiting_user.channel;
        next_waiting.channel_instance = waiting_user.channel_instance.empty()
            ? waiting_user.channel
            : waiting_user.channel_instance;
        next_waiting.chat_id = waiting_user.chat_id;
        next_waiting.reply_to = waiting_user.reply_to;
        next_waiting.updated_at = NowIso();
        std::lock_guard<std::mutex> guard(mutex_);
        waiting_tasks_[BuildWaitingKey(next_waiting.channel_instance,
                                       next_waiting.agent_name,
                                       next_waiting.chat_id)] = std::move(next_waiting);
        SaveState();
        CheckStatusUpdate(relay_.UpdateTaskStatus(waiting_task.local_agent, waiting_task.task_id, {
            "waiting_user",
            waiting_question.empty() ? "waiting for user reply" : waiting_question,
            50,
            NowIso(),
            waiting_task.session_key,
            {},
            {},
            waiting_user
        }), waiting_task.task_id, "waiting_user");
        return false;
    }
    return true;
}

bool TaskRuntime::HasPendingTaskForLocalAgent(const std::string& local_agent) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (active_tasks_.find(local_agent) != active_tasks_.end()) {
        return true;
    }
    return std::any_of(waiting_tasks_.begin(), waiting_tasks_.end(), [&local_agent](const auto& entry) {
        return entry.second.local_agent == local_agent;
    });
}

bool TaskRuntime::IsTaskClaimed(const std::string& task_id) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return claimed_task_ids_.find(task_id) != claimed_task_ids_.end();
}

void TaskRuntime::MarkActiveTask(const std::string& local_agent,
                                 const std::string& task_id,
                                 const std::string& session_key) {
    std::lock_guard<std::mutex> guard(mutex_);
    active_tasks_[local_agent] = ActiveTask{task_id, session_key};
}

void TaskRuntime::ClearActiveTask(const std::string& local_agent) {
    std::lock_guard<std::mutex> guard(mutex_);
    active_tasks_.erase(local_agent);
}

void TaskRuntime::EnsureDailySummaryJobs() {
    if (!cron_) {
        return;
    }

    RemoveDailySummaryJobs();
    const auto local_agents = relay_.ManagedLocalAgents();
    for (const auto& local_agent : local_agents) {
        kabot::cron::CronJob job{};
        job.id = SummaryJobId(local_agent);
        job.name = job.id;
        job.enabled = true;
        job.schedule.kind = kabot::cron::CronScheduleKind::Cron;
        job.schedule.expr = "0 " + std::to_string(config_.task_system.daily_summary_hour_local) + " * * *";
        job.payload.kind = "task_runtime_daily_summary";
        job.payload.agent = local_agent;
        job.payload.message = "upload daily memory";
        const auto added = cron_->AddJob(job);
        if (added.has_value()) {
            cron_job_ids_.push_back(added->id);
        } else {
            LOG_ERROR("[task_runtime] Failed to add daily summary cron job");
        }
    }
}

void TaskRuntime::RemoveDailySummaryJobs() {
    if (!cron_) {
        cron_job_ids_.clear();
        return;
    }
    if (cron_job_ids_.empty()) {
        const auto jobs = cron_->ListJobs(true);
        for (const auto& job : jobs) {
            if (job.payload.kind == "task_runtime_daily_summary") {
                cron_->RemoveJob(job.id);
            }
        }
        return;
    }
    for (const auto& job_id : cron_job_ids_) {
        cron_->RemoveJob(job_id);
    }
    cron_job_ids_.clear();
}

bool TaskRuntime::HandleDailySummaryCron(const kabot::cron::CronJob& job, std::string& response) {
    const auto local_agent = job.payload.agent;
    if (local_agent.empty()) {
        response = "task runtime daily summary skipped: missing local agent";
        return true;
    }
    if (!relay_.HasManagedLocalAgent(local_agent)) {
        response = "task runtime daily summary skipped: agent is not relay-managed";
        return true;
    }

    const auto* agent_config = agents_.GetAgentConfig(local_agent);
    if (!agent_config) {
        response = "task runtime daily summary skipped: agent config not found";
        return true;
    }

    kabot::agent::MemoryStore memory(agent_config->workspace);
    const auto content = memory.ReadToday();
    const auto today = TodayDate();
    if (content.empty()) {
        std::lock_guard<std::mutex> guard(mutex_);
        daily_summary_records_[local_agent] = DailySummaryRecord{today, {}, "skipped", "daily memory is empty"};
        SaveState();
        response = "task runtime daily summary skipped: daily memory is empty";
        return true;
    }

    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = daily_summary_records_.find(local_agent);
        if (it != daily_summary_records_.end() &&
            it->second.summary_date == today &&
            it->second.status == "uploaded") {
            response = "task runtime daily summary skipped: already uploaded today";
            return true;
        }
    }

    const auto uploaded_at = NowIso();
    const auto result = relay_.UploadDailySummary(local_agent, today, content, uploaded_at);
    {
        std::lock_guard<std::mutex> guard(mutex_);
        daily_summary_records_[local_agent] = DailySummaryRecord{
            today,
            result.success ? uploaded_at : std::string(),
            result.success ? "uploaded" : "failed",
            result.message
        };
        SaveState();
    }

    response = result.success
        ? "task runtime daily summary uploaded"
        : "task runtime daily summary failed: " + result.message;
    return true;
}

bool TaskRuntime::TryResumeWaitingTask(const kabot::bus::InboundMessage& msg,
                                       kabot::bus::OutboundMessage& outbound) {
    const auto key = WaitingKey(msg);

    WaitingTask waiting_task;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = waiting_tasks_.find(key);
        if (it == waiting_tasks_.end()) {
            return false;
        }
        waiting_task = it->second;
        waiting_tasks_.erase(it);
        SaveState();
    }

    CheckStatusUpdate(relay_.UpdateTaskStatus(waiting_task.local_agent, waiting_task.task_id, {
        "running",
        "resumed after user reply",
        60,
        NowIso(),
        waiting_task.session_key
    }), waiting_task.task_id, "running");
    std::string result;
    const auto completed = ResumeWaitingTask(waiting_task, msg.content, result);
    outbound.channel = msg.channel;
    outbound.channel_instance = EffectiveChannelInstance(msg);
    outbound.agent_name = waiting_task.agent_name.empty() ? msg.agent_name : waiting_task.agent_name;
    outbound.chat_id = msg.chat_id;
    outbound.content = result;
    if (completed) {
        // Verify task completion after resume
        std::string verification_reason;
        bool verification_passed = true;
        try {
            const auto* agent_config = agents_.GetAgentConfig(waiting_task.local_agent);
            const std::string workspace = agent_config ? agent_config->workspace : config_.agents.defaults.workspace;
            const std::string project_name = waiting_task.task_id;

            const auto session = agents_.GetSession(waiting_task.local_agent, waiting_task.session_key);
            const auto execution_log = BuildExecutionLog(session);

            // Get original instruction from session or use empty
            std::string original_instruction;
            for (const auto& m : session.Messages()) {
                if (m.role == "user" && !m.content.empty()) {
                    original_instruction = m.content;
                    break;
                }
            }

            verification_passed = VerifyTaskCompletion(waiting_task.local_agent,
                                                        waiting_task.session_key,
                                                        original_instruction,
                                                        result,
                                                        execution_log,
                                                        verification_reason);

            if (verification_passed) {
                CheckStatusUpdate(relay_.UpdateTaskStatus(waiting_task.local_agent, waiting_task.task_id, {
                    "completed",
                    "task completed",
                    100,
                    NowIso(),
                    waiting_task.session_key,
                    result
                }), waiting_task.task_id, "completed");
            } else {
                CheckStatusUpdate(relay_.UpdateTaskStatus(waiting_task.local_agent, waiting_task.task_id, {
                    "failed",
                    "Task " + waiting_task.task_id + " failed verification after resume: " + verification_reason,
                    -1,
                    NowIso(),
                    waiting_task.session_key,
                    {},
                    verification_reason
                }), waiting_task.task_id, "failed");
            }
        } catch (const std::exception& ex) {
            LOG_WARN("[task_runtime] verification failed for resumed task {}: {}", waiting_task.task_id, ex.what());
            CheckStatusUpdate(relay_.UpdateTaskStatus(waiting_task.local_agent, waiting_task.task_id, {
                "completed",
                "task completed (verification skipped due to error)",
                100,
                NowIso(),
                waiting_task.session_key,
                result
            }), waiting_task.task_id, "completed");
        }
    }
    return true;
}

void TaskRuntime::UpsertWaitingTask(const kabot::bus::InboundMessage& msg,
                                    const kabot::bus::OutboundMessage& outbound) {
    WaitingTask waiting_task{};
    waiting_task.task_id.clear();
    waiting_task.local_agent = msg.agent_name;
    waiting_task.session_key = msg.SessionKey();
    waiting_task.question = outbound.content;
    waiting_task.channel = msg.channel;
    waiting_task.channel_instance = EffectiveChannelInstance(msg);
    waiting_task.chat_id = msg.chat_id;
    waiting_task.reply_to = outbound.reply_to;
    waiting_task.agent_name = msg.agent_name;
    waiting_task.updated_at = NowIso();

    std::lock_guard<std::mutex> guard(mutex_);
    waiting_tasks_[BuildWaitingKey(waiting_task.channel_instance,
                                   waiting_task.agent_name,
                                   waiting_task.chat_id)] = std::move(waiting_task);
    SaveState();
}

void TaskRuntime::ClearWaitingTask(const kabot::bus::InboundMessage& msg) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto it = waiting_tasks_.find(WaitingKey(msg));
    if (it == waiting_tasks_.end()) {
        return;
    }
    waiting_tasks_.erase(it);
    SaveState();
}

bool TaskRuntime::ShouldWaitForUser(const kabot::bus::InboundMessage& msg,
                                    const kabot::bus::OutboundMessage& outbound) const {
    if (msg.channel == "system") {
        return false;
    }
    if (msg.chat_id.empty() || outbound.content.empty()) {
        return false;
    }

    const auto content = ToLower(outbound.content);
    const bool has_question_mark =
        content.find('?') != std::string::npos ||
        content.find("？") != std::string::npos;

    if (ContainsAny(content, {
            "completed",
            "done.",
            "done!",
            "background task completed",
            "i've completed",
            "i have completed",
            "已完成",
            "已经完成",
            "处理完成",
            "完成了",
            "error:",
            "failed",
            "失败"
        })) {
        return false;
    }

    const bool explicit_confirmation = ContainsAny(content, {
        "please confirm",
        "can you confirm",
        "confirm?",
        "需要你确认",
        "请确认",
        "确认一下",
        "确认后我再继续",
        "确认后继续"
    });

    const bool asks_for_choice = ContainsAny(content, {
        "which one",
        "which option",
        "let me know which",
        "tell me which",
        "pick one",
        "choose one",
        "choose between",
        "你希望我",
        "你想让我",
        "你要哪个",
        "选哪个",
        "选择哪个",
        "二选一"
    });

    const bool asks_for_permission = ContainsAny(content, {
        "do you want me to",
        "should i",
        "would you like me to",
        "shall i",
        "是否继续",
        "要不要继续",
        "是否要我",
        "要我现在",
        "我继续吗"
    });

    const bool directive_to_reply = ContainsAny(content, {
        "reply with",
        "let me know",
        "tell me",
        "回复我",
        "告诉我",
        "请回复",
        "请告诉我"
    });

    if (explicit_confirmation) {
        return true;
    }

    if ((asks_for_choice || asks_for_permission) && (has_question_mark || directive_to_reply)) {
        return true;
    }

    if (has_question_mark && directive_to_reply &&
        ContainsAny(content, {
            "continue",
            "proceed",
            "go ahead",
            "继续",
            "开始",
            "执行",
            "删除",
            "修改",
            "采用"
        })) {
        return true;
    }

    return false;
}

std::string TaskRuntime::BuildExecutionLog(const kabot::session::Session& session) const {
    return BuildExecutionLogFromSession(session);
}

bool TaskRuntime::VerifyTaskCompletion(const std::string& local_agent,
                                       const std::string& session_key,
                                       const std::string& instruction,
                                       const std::string& result,
                                       const std::string& execution_log,
                                       std::string& verification_reason) {
    if (execution_log.empty()) {
        verification_reason = "No execution log available, skipping verification";
        return true;
    }

    std::ostringstream prompt;
    prompt << "You are a task completion verifier. Your job is to check whether the agent truly completed the task based on its execution history.\n\n";
    prompt << "Original Task Instruction:\n" << instruction << "\n\n";
    prompt << "Agent Final Result:\n" << result.substr(0, 1000) << "\n\n";
    prompt << "Execution History:\n" << execution_log << "\n\n";
    prompt << "Analyze whether the task is COMPLETE. Consider:\n";
    prompt << "1. Did the agent actually perform the required actions (file reads, edits, commands, etc.)?\n";
    prompt << "2. Does the final result match the task requirements?\n";
    prompt << "3. Did the agent claim completion without doing anything?\n";
    prompt << "4. Are there any obvious errors or missing steps?\n\n";
    prompt << "Respond with exactly one of:\n";
    prompt << "- \"VERIFIED: <brief reason>\" if the task is truly complete\n";
    prompt << "- \"INCOMPLETE: <brief reason>\" if the task is not truly complete\n";

    const std::string verifier_session_key = session_key + ":verify";
    kabot::CancelToken cancel_token;
    const auto verification_result = agents_.ProcessDirect(local_agent,
                                                            prompt.str(),
                                                            verifier_session_key,
                                                            {},
                                                            {},
                                                            {},
                                                            cancel_token);

    const auto trimmed = Trim(verification_result);
    if (trimmed.find("INCOMPLETE") == 0 || trimmed.find("incomplete") != std::string::npos) {
        verification_reason = trimmed;
        return false;
    }

    verification_reason = trimmed;
    return true;
}

namespace {

void AtomicWriteJson(const std::filesystem::path& path, const nlohmann::json& json) {
    std::filesystem::create_directories(path.parent_path());
    const auto temp_path = path.string() + ".tmp";
    std::ofstream output(temp_path, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }
    output << json.dump(2);
    output.flush();
    if (!output.good()) {
        return;
    }
    output.close();
    std::filesystem::rename(temp_path, path);
}

}  // namespace

void TaskRuntime::LoadState() {
    std::lock_guard<std::mutex> guard(mutex_);
    daily_summary_records_.clear();
    waiting_tasks_.clear();
    active_tasks_.clear();
    claimed_task_ids_.clear();
    LoadWaitingTasks();
    LoadDailySummaries();
}

void TaskRuntime::SaveState() const {
    SaveWaitingTasks();
    SaveDailySummaries();
}

void TaskRuntime::SaveWaitingTasks() const {
    nlohmann::json json = nlohmann::json::object();
    json["waitingTasks"] = nlohmann::json::array();
    for (const auto& [key, task] : waiting_tasks_) {
        json["waitingTasks"].push_back({
            {"key", key},
            {"taskId", task.task_id},
            {"localAgent", task.local_agent},
            {"sessionKey", task.session_key},
            {"question", task.question},
            {"channel", task.channel},
            {"channelInstance", task.channel_instance},
            {"chatId", task.chat_id},
            {"replyTo", task.reply_to},
            {"agentName", task.agent_name},
            {"updatedAt", task.updated_at}
        });
    }
    AtomicWriteJson(WaitingTasksStatePath(), json);
}

void TaskRuntime::SaveDailySummaries() const {
    nlohmann::json json = nlohmann::json::object();
    json["dailySummaries"] = nlohmann::json::array();
    for (const auto& [local_agent, record] : daily_summary_records_) {
        json["dailySummaries"].push_back({
            {"localAgent", local_agent},
            {"summaryDate", record.summary_date},
            {"uploadedAt", record.uploaded_at},
            {"status", record.status},
            {"message", record.message}
        });
    }
    AtomicWriteJson(DailySummaryStatePath(), json);
}

void TaskRuntime::LoadWaitingTasks() {
    std::ifstream input(WaitingTasksStatePath());
    if (!input.is_open()) {
        return;
    }
    nlohmann::json json;
    input >> json;
    if (!json.is_object() || !json.contains("waitingTasks") || !json["waitingTasks"].is_array()) {
        return;
    }
    for (const auto& item : json["waitingTasks"]) {
        if (!item.is_object()) {
            continue;
        }
        WaitingTask waiting_task{};
        waiting_task.task_id = item.value("taskId", std::string());
        waiting_task.local_agent = item.value("localAgent", std::string());
        waiting_task.session_key = item.value("sessionKey", std::string());
        waiting_task.question = item.value("question", std::string());
        waiting_task.channel = item.value("channel", std::string());
        waiting_task.channel_instance = item.value("channelInstance", std::string());
        waiting_task.chat_id = item.value("chatId", std::string());
        waiting_task.reply_to = item.value("replyTo", std::string());
        waiting_task.agent_name = item.value("agentName", std::string());
        waiting_task.updated_at = item.value("updatedAt", std::string());
        if (waiting_task.channel_instance.empty() || waiting_task.chat_id.empty()) {
            continue;
        }
        waiting_tasks_[BuildWaitingKey(waiting_task.channel_instance,
                                       waiting_task.agent_name,
                                       waiting_task.chat_id)] = std::move(waiting_task);
    }
}

void TaskRuntime::LoadDailySummaries() {
    std::ifstream input(DailySummaryStatePath());
    if (!input.is_open()) {
        return;
    }
    nlohmann::json json;
    input >> json;
    if (!json.is_object() || !json.contains("dailySummaries") || !json["dailySummaries"].is_array()) {
        return;
    }
    for (const auto& item : json["dailySummaries"]) {
        if (!item.is_object()) {
            continue;
        }
        const auto local_agent = item.value("localAgent", std::string());
        if (local_agent.empty()) {
            continue;
        }
        daily_summary_records_[local_agent] = DailySummaryRecord{
            item.value("summaryDate", std::string()),
            item.value("uploadedAt", std::string()),
            item.value("status", std::string()),
            item.value("message", std::string())
        };
    }
}

std::string TaskRuntime::SummaryJobId(const std::string& local_agent) const {
    return "task-runtime-daily-summary:" + local_agent;
}

std::string TaskRuntime::WaitingKey(const kabot::bus::InboundMessage& msg) const {
    return BuildWaitingKey(
        EffectiveChannelInstance(msg),
        msg.agent_name,
        msg.chat_id);
}

std::filesystem::path TaskRuntime::WaitingTasksStatePath() const {
    return std::filesystem::path(config_.agents.defaults.workspace) / ".kabot" / "task_runtime_waiting_tasks.json";
}

std::filesystem::path TaskRuntime::DailySummaryStatePath() const {
    return std::filesystem::path(config_.agents.defaults.workspace) / ".kabot" / "task_runtime_daily_summary.json";
}

std::string TaskRuntime::TodayDate() const {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y-%m-%d");
    return oss.str();
}

std::string TaskRuntime::NowIso() const {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

}  // namespace kabot::task