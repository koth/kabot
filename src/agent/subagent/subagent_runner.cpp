#include "agent/subagent/subagent_runner.hpp"

#include <thread>

#include "agent/subagent/async_attribution.hpp"
#include "agent/subagent/subagent_tool_filter.hpp"
#include "utils/logging.hpp"

namespace kabot::subagent {

SubagentRunner::SubagentRunner(kabot::providers::LLMProvider& provider,
                               kabot::agent::tools::ToolRegistry& tools,
                               SubagentTaskManager& task_manager,
                               SubagentTranscriptStore& transcript_store,
                               std::string workspace,
                               kabot::config::AgentDefaults defaults)
    : provider_(provider)
    , tools_(tools)
    , task_manager_(task_manager)
    , transcript_store_(transcript_store)
    , workspace_(std::move(workspace))
    , defaults_(std::move(defaults)) {}

std::string SubagentRunner::RunSync(const RunAgentParams& params,
                                    const SubagentMessageHandler& on_message,
                                    const std::string& task_id) {
    return DoRun(params, on_message, task_id);
}

std::string SubagentRunner::RunAsync(const RunAgentParams& params) {
    std::string task_id = task_manager_.RegisterTask(
        params.tool_use_context.agent_id,
        params.description);
    
    task_manager_.UpdateStatus(task_id, SubagentStatus::kBackgrounded);
    
    AgentTranscriptMetadata meta;
    meta.agent_id = params.tool_use_context.agent_id;
    meta.agent_type = params.agent_definition.agent_type;
    meta.description = params.description;
    meta.worktree_path = params.worktree_path;
    meta.parent_agent_id = params.tool_use_context.parent_agent_id;
    meta.parent_session_id = params.tool_use_context.parent_session_id;
    meta.invocation_kind = "spawn";
    meta.created_at = std::chrono::steady_clock::now();
    transcript_store_.WriteMetadata(meta);
    transcript_store_.AppendMessages(params.tool_use_context.agent_id, params.prompt_messages);
    
    std::thread([this, params, task_id]() {
        DoRunAsync(params, task_id);
    }).detach();
    
    return task_id;
}

void SubagentRunner::DoRunAsync(const RunAgentParams& params,
                                const std::string& task_id) {
    task_manager_.UpdateStatus(task_id, SubagentStatus::kRunningForeground);
    
    AsyncAttribution::Set({
        params.tool_use_context.agent_id,
        params.tool_use_context.parent_session_id,
        params.agent_definition.agent_type,
        params.description,
        true,
        {},
        "spawn"
    });
    
    try {
        auto result = DoRun(params, {}, task_id);
        task_manager_.MarkCompleted(task_id, result);
    } catch (const std::exception& ex) {
        task_manager_.MarkFailed(task_id, "runtime_error", ex.what(), false);
    } catch (...) {
        task_manager_.MarkFailed(task_id, "unknown", "unknown error", false);
    }
    
    AsyncAttribution::Clear();
}

std::string SubagentRunner::DoRun(const RunAgentParams& params,
                                  const SubagentMessageHandler& on_message,
                                  const std::string& task_id) {
    const auto& agent_def = params.agent_definition;
    const auto& child_ctx = params.tool_use_context;
    
    std::string model = agent_def.model;
    if (model.empty() || model == "inherit") {
        model = defaults_.model.empty() ? provider_.GetDefaultModel() : defaults_.model;
    }
    if (!params.model.empty()) {
        model = params.model;
    }
    
    auto messages = params.prompt_messages;
    if (!params.fork_context_messages.empty()) {
        messages.insert(messages.begin(),
                        params.fork_context_messages.begin(),
                        params.fork_context_messages.end());
    }
    
    if (messages.empty() || messages[0].role != "system") {
        kabot::providers::Message sys_msg;
        sys_msg.role = "system";
        sys_msg.content = agent_def.get_system_prompt ? agent_def.get_system_prompt() : "";
        messages.insert(messages.begin(), sys_msg);
    } else if (agent_def.get_system_prompt) {
        messages[0].content = agent_def.get_system_prompt();
    }
    
    auto all_tools = tools_.GetDefinitions();
    auto resolved_tools = ResolveAgentTools(
        all_tools, agent_def, params.is_async, params.allowed_tools);
    
    int max_turns = params.max_turns > 0 ? params.max_turns :
                    (agent_def.max_turns > 0 ? agent_def.max_turns : defaults_.max_iterations);
    
    transcript_store_.AppendMessages(child_ctx.agent_id, messages);
    
    int turns = 0;
    std::string final_content;
    
    LOG_INFO("[subagent] run agent_id={} type={} model={} async={} max_turns={}",
             child_ctx.agent_id, agent_def.agent_type, model,
             params.is_async ? "true" : "false", max_turns);
    
    while (turns < max_turns) {
        turns++;
        
        if (task_manager_.GetTask(task_id) &&
            task_manager_.GetTask(task_id)->status == SubagentStatus::kAborted) {
            LOG_INFO("[subagent] aborted agent_id={}", child_ctx.agent_id);
            throw std::runtime_error("subagent aborted");
        }
        
        auto response = provider_.Chat(
            messages,
            resolved_tools,
            model,
            defaults_.max_tokens,
            defaults_.temperature);
        
        if (on_message) {
            SubagentMessage msg;
            msg.type = "assistant";
            msg.content = response.content;
            msg.raw_message.role = "assistant";
            msg.raw_message.content = response.content;
            msg.raw_message.tool_calls = response.tool_calls;
            on_message(msg);
        }
        
        if (response.HasToolCalls()) {
            kabot::providers::Message assistant_msg;
            assistant_msg.role = "assistant";
            assistant_msg.content = response.content;
            assistant_msg.tool_calls = response.tool_calls;
            messages.push_back(assistant_msg);
            
            transcript_store_.AppendMessage(child_ctx.agent_id, messages.back());
            
            for (const auto& call : response.tool_calls) {
                if (params.can_use_tool && !params.can_use_tool(call.name)) {
                    std::string err = "Error: tool '" + call.name + "' is not allowed for this subagent";
                    LOG_WARN("[subagent] tool_denied agent_id={} tool={}", child_ctx.agent_id, call.name);
                    
                    kabot::providers::Message tool_msg;
                    tool_msg.role = "tool";
                    tool_msg.tool_call_id = call.id;
                    tool_msg.name = call.name;
                    tool_msg.content = err;
                    messages.push_back(tool_msg);
                    transcript_store_.AppendMessage(child_ctx.agent_id, tool_msg);
                    
                    if (on_message) {
                        SubagentMessage tm;
                        tm.type = "tool_result";
                        tm.content = err;
                        tm.tool_name = call.name;
                        tm.tool_call_id = call.id;
                        tm.raw_message = tool_msg;
                        on_message(tm);
                    }
                    continue;
                }
                
                std::string result;
                try {
                    result = tools_.Execute(call.name, call.arguments);
                } catch (const std::exception& ex) {
                    result = std::string("Error: ") + ex.what();
                }
                
                kabot::providers::Message tool_msg;
                tool_msg.role = "tool";
                tool_msg.tool_call_id = call.id;
                tool_msg.name = call.name;
                tool_msg.content = result;
                messages.push_back(tool_msg);
                transcript_store_.AppendMessage(child_ctx.agent_id, tool_msg);
                
                if (on_message) {
                    SubagentMessage tm;
                    tm.type = "tool_result";
                    tm.content = result;
                    tm.tool_name = call.name;
                    tm.tool_call_id = call.id;
                    tm.raw_message = tool_msg;
                    on_message(tm);
                }
                
                if (task_manager_.GetTask(task_id)) {
                    AgentTaskRecord::Progress prog;
                    prog.last_tool_name = call.name;
                    prog.turns = turns;
                    task_manager_.UpdateProgress(task_id, prog);
                }
            }
        } else {
            final_content = response.content;
            break;
        }
    }
    
    if (final_content.empty()) {
        final_content = "Background task completed.";
    }
    
    kabot::providers::Message final_msg;
    final_msg.role = "assistant";
    final_msg.content = final_content;
    transcript_store_.AppendMessage(child_ctx.agent_id, final_msg);
    
    LOG_INFO("[subagent] completed agent_id={} turns={}", child_ctx.agent_id, turns);
    return final_content;
}

std::string SubagentRunner::Resume(const std::string& agent_id,
                                   const SubagentMessageHandler& on_message) {
    auto meta = transcript_store_.LoadMetadata(agent_id);
    if (meta.agent_id.empty()) {
        throw std::runtime_error("Cannot resume: metadata not found for agent " + agent_id);
    }
    
    auto messages = transcript_store_.LoadMessages(agent_id);
    if (messages.empty()) {
        throw std::runtime_error("Cannot resume: transcript empty for agent " + agent_id);
    }
    
    RunAgentParams params;
    params.agent_definition.agent_type = meta.agent_type;
    params.agent_definition.get_system_prompt = []() { return ""; };
    params.agent_definition.max_turns = defaults_.max_iterations;
    params.prompt_messages = messages;
    params.tool_use_context.agent_id = meta.agent_id;
    params.tool_use_context.parent_agent_id = meta.parent_agent_id;
    params.tool_use_context.parent_session_id = meta.parent_session_id;
    params.worktree_path = meta.worktree_path;
    params.is_async = true;
    
    auto task = task_manager_.GetTaskByAgentId(agent_id);
    std::string task_id = task ? task->task_id : "";
    if (task_id.empty()) {
        task_id = task_manager_.RegisterTask(agent_id, meta.description);
    }
    task_manager_.UpdateStatus(task_id, SubagentStatus::kResumedRunning);
    
    meta.invocation_kind = "resume";
    transcript_store_.WriteMetadata(meta);
    
    std::thread([this, params, task_id, on_message]() {
        AsyncAttribution::Set({
            params.tool_use_context.agent_id,
            params.tool_use_context.parent_session_id,
            params.agent_definition.agent_type,
            params.description,
            true,
            {},
            "resume"
        });
        try {
            auto result = DoRun(params, on_message, task_id);
            task_manager_.MarkCompleted(task_id, result);
        } catch (const std::exception& ex) {
            task_manager_.MarkFailed(task_id, "resume_error", ex.what(), false);
        }
        AsyncAttribution::Clear();
    }).detach();
    
    return task_id;
}

} // namespace kabot::subagent
