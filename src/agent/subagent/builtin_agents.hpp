#pragma once

#include "agent/subagent/subagent_types.hpp"

namespace kabot::subagent {

AgentDefinition ExploreAgent();
AgentDefinition ForkAgent();
AgentDefinition DefaultSubagent();

const char* ExploreAgentName();
const char* ForkAgentName();

} // namespace kabot::subagent
