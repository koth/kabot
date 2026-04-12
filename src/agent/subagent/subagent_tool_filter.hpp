#pragma once

#include <vector>
#include <string>

#include "agent/subagent/subagent_types.hpp"
#include "providers/llm_provider.hpp"

namespace kabot::subagent {

std::vector<kabot::providers::ToolDefinition> ResolveAgentTools(
    const std::vector<kabot::providers::ToolDefinition>& base_pool,
    const AgentDefinition& agent_definition,
    bool is_async,
    const std::vector<std::string>& allowed_tools);

} // namespace kabot::subagent
