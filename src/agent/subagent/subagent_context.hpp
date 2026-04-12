#pragma once

#include "agent/subagent/subagent_types.hpp"

namespace kabot::subagent {

SubagentContext CreateSubagentContext(
    const SubagentContext& parent,
    const std::string& agent_id,
    const std::vector<kabot::providers::Message>& initial_messages,
    const std::string& parent_session_id = "");

} // namespace kabot::subagent
