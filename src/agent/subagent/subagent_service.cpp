#include "agent/subagent/subagent_service.hpp"

#include <random>
#include <sstream>

#include "agent/subagent/builtin_agents.hpp"
#include "agent/subagent/subagent_context.hpp"
#include "agent/subagent/subagent_tool_filter.hpp"
#include "utils/logging.hpp"

namespace kabot::subagent {

namespace {
std::string GenerateAgentId(const std::string& prefix) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> dis(0, 15);
    std::stringstream ss;
    ss << prefix << "_";
    for (int i = 0; i < 8; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

AgentDefinition ResolveAgentDefinition(const std::string& subagent_type) {
    if (subagent_type == ExploreAgentName()) {
        return ExploreAgent();
    }
    if (subagent_type == ForkAgentName()) {
        return ForkAgent();
    }
    return DefaultSubagent();
}

std::vector<kabot::providers::Message> BuildPromptMessages(const std::string& prompt) {
    std::vector<kabot::providers::Message> messages;
    kabot::providers::Message msg;
    msg.role = "user";
    msg.content = prompt;
    messages.push_back(msg);
    return messages;
}

} // namespace

SubagentService::SubagentService(kabot::providers::LLMProvider& provider,
                                 kabot::agent::tools::ToolRegistry& tools,
                                 std::string workspace,
                                 kabot::config::AgentDefaults defaults)
    : provider_(provider)
    , tools_(tools)
    , transcript_store_(std::move(workspace))
    , runner_(std::make_unique<SubagentRunner>(
          provider, tools, task_manager_, transcript_store_,
          transcript_store_.MetadataPath("").parent_path().parent_path().string(),
          std::move(defaults))) {}

SubagentService::SpawnResult SubagentService::Spawn(
    const AgentSpawnInput& input,
    const SubagentContext& parent_ctx) {
    
    auto agent_def = ResolveAgentDefinition(input.subagent_type);
    bool should_run_async = input.run_in_background || agent_def.background;
    
    auto prompt_messages = BuildPromptMessages(input.prompt);
    if (!agent_def.initial_prompt.empty()) {
        kabot::providers::Message sys;
        sys.role = "system";
        sys.content = agent_def.initial_prompt;
        prompt_messages.insert(prompt_messages.begin(), sys);
    }
    
    std::string agent_id = GenerateAgentId(agent_def.agent_type);
    auto child_ctx = CreateSubagentContext(parent_ctx, agent_id, prompt_messages);
    
    auto base_tools = tools_.GetDefinitions();
    auto worker_tools = ResolveAgentTools(base_tools, agent_def, should_run_async, {});
    
    RunAgentParams run_params;
    run_params.agent_definition = agent_def;
    run_params.prompt_messages = prompt_messages;
    run_params.tool_use_context = child_ctx;
    run_params.available_tools = worker_tools;
    run_params.is_async = should_run_async;
    run_params.query_source = "subagent:" + agent_def.agent_type;
    run_params.model = input.model;
    run_params.max_turns = agent_def.max_turns;
    run_params.description = input.description;
    run_params.worktree_path = input.cwd;
    
    if (should_run_async) {
        std::string task_id = runner_->RunAsync(run_params);
        return SpawnResult{"async_launched", task_id, agent_id, "", 0, 0, 0, ""};
    }

    auto summary = runner_->RunSync(run_params);
    SpawnResult result;
    result.type = "sync_result";
    result.agent_id = summary.agent_id;
    result.result = summary.result;
    result.total_tokens = summary.total_tokens;
    result.tool_calls_count = summary.tool_calls_count;
    result.duration_ms = summary.duration_ms;
    result.worktree_path = summary.worktree_path;
    return result;
}

std::string SubagentService::Resume(const std::string& agent_id,
                                    const SubagentMessageHandler& on_message) {
    return runner_->Resume(agent_id, on_message);
}

std::vector<AgentTaskRecord> SubagentService::ListTasks() const {
    return task_manager_.ListTasks();
}

} // namespace kabot::subagent
