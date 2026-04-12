#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "providers/llm_provider.hpp"

namespace kabot::subagent {

enum class SubagentStatus {
    kIdle,
    kSpawning,
    kRunningForeground,
    kBackgrounded,
    kCompleted,
    kFailed,
    kAborted,
    kResumedRunning,
};

inline bool IsTerminalStatus(SubagentStatus status) {
    return status == SubagentStatus::kCompleted ||
           status == SubagentStatus::kFailed ||
           status == SubagentStatus::kAborted;
}

struct AgentDefinition {
    std::string agent_type;
    std::string when_to_use;
    std::function<std::string()> get_system_prompt;
    std::vector<std::string> tools;
    std::vector<std::string> disallowed_tools;
    std::string model;
    std::string permission_mode = "default";
    int max_turns = 20;
    bool background = false;
    std::string memory = "none";
    std::string isolation = "none";
    std::vector<std::string> skills;
    std::string initial_prompt;
    std::unordered_map<std::string, std::string> metadata;
};

struct AgentSpawnInput {
    std::string prompt;
    std::string subagent_type;
    std::string description;
    std::string model;
    bool run_in_background = false;
    std::string name;
    std::string team_name;
    std::string mode = "spawn";
    std::string isolation = "none";
    std::string cwd;
};

struct SubagentContext {
    std::string agent_id;
    std::vector<kabot::providers::Message> messages;
    std::function<std::unordered_map<std::string, std::string>()> get_app_state;
    std::function<void(const std::unordered_map<std::string, std::string>&)> set_app_state;
    std::function<void(int)> set_response_length;
    std::function<void(int)> push_api_metrics_entry;
    bool preserve_tool_use_results = false;
    bool should_avoid_permission_prompts = true;
    std::string parent_agent_id;
    std::string parent_session_id;
};

struct RunAgentParams {
    AgentDefinition agent_definition;
    std::vector<kabot::providers::Message> prompt_messages;
    SubagentContext tool_use_context;
    std::vector<kabot::providers::ToolDefinition> available_tools;
    std::function<bool(const std::string&)> can_use_tool;
    bool is_async = false;
    std::string query_source;
    std::string model;
    int max_turns = 20;
    std::vector<std::string> allowed_tools;
    std::vector<kabot::providers::Message> fork_context_messages;
    bool use_exact_tools = false;
    std::string description;
    std::string worktree_path;
    std::function<void()> on_query_progress;
};

struct AgentTaskRecord {
    std::string task_id;
    std::string agent_id;
    std::string description;
    SubagentStatus status = SubagentStatus::kIdle;
    std::chrono::steady_clock::time_point started_at;
    std::optional<std::chrono::steady_clock::time_point> finished_at;
    std::string output_file;
    struct Progress {
        std::string message;
        std::string last_tool_name;
        int tokens = 0;
        int turns = 0;
    } progress;
    struct ErrorInfo {
        std::string code;
        std::string message;
        bool retryable = false;
    } error;
};

struct AgentTranscriptMetadata {
    std::string agent_id;
    std::string agent_type;
    std::string description;
    std::string worktree_path;
    std::string parent_agent_id;
    std::string parent_session_id;
    std::string invocation_kind = "spawn";
    std::chrono::steady_clock::time_point created_at;
};

struct SubagentMessage {
    std::string type;
    std::string content;
    std::string tool_name;
    std::string tool_call_id;
    kabot::providers::Message raw_message;
};

using SubagentMessageHandler = std::function<void(const SubagentMessage&)>;
using SubagentCompletionHandler = std::function<void(bool success, const std::string& result, const std::string& error)>;

} // namespace kabot::subagent
