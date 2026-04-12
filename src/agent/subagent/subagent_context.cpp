#include "agent/subagent/subagent_context.hpp"

namespace kabot::subagent {

SubagentContext CreateSubagentContext(
    const SubagentContext& parent,
    const std::string& agent_id,
    const std::vector<kabot::providers::Message>& initial_messages,
    const std::string& parent_session_id) {
    
    SubagentContext child;
    child.agent_id = agent_id;
    child.messages = initial_messages;
    
    child.get_app_state = parent.get_app_state;
    child.set_app_state = parent.set_app_state;
    child.set_response_length = parent.set_response_length;
    child.push_api_metrics_entry = parent.push_api_metrics_entry;
    
    child.preserve_tool_use_results = false;
    child.should_avoid_permission_prompts = true;
    child.parent_agent_id = parent.agent_id.empty() ? "main" : parent.agent_id;
    child.parent_session_id = parent_session_id.empty() ? parent.parent_session_id : parent_session_id;
    
    return child;
}

} // namespace kabot::subagent
