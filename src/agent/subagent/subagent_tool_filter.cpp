#include "agent/subagent/subagent_tool_filter.hpp"

#include <algorithm>
#include <unordered_set>

#include "utils/logging.hpp"

namespace kabot::subagent {

std::vector<kabot::providers::ToolDefinition> ResolveAgentTools(
    const std::vector<kabot::providers::ToolDefinition>& base_pool,
    const AgentDefinition& agent_definition,
    bool is_async,
    const std::vector<std::string>& allowed_tools) {
    
    const std::unordered_set<std::string> globally_disallowed = {};
    const std::unordered_set<std::string> async_whitelist = {
        "read_file", "list_dir", "glob", "grep", "web_search", "web_fetch", "bash"
    };
    const std::unordered_set<std::string> system_tools = {
        "read_file", "write_file", "edit_file", "list_dir", "bash"
    };
    
    std::vector<kabot::providers::ToolDefinition> result;
    
    for (const auto& tool : base_pool) {
        if (globally_disallowed.count(tool.name)) {
            continue;
        }
        
        if (!agent_definition.disallowed_tools.empty()) {
            if (std::find(agent_definition.disallowed_tools.begin(),
                          agent_definition.disallowed_tools.end(),
                          tool.name) != agent_definition.disallowed_tools.end()) {
                continue;
            }
        }
        
        if (!agent_definition.tools.empty()) {
            bool has_wildcard = std::find(agent_definition.tools.begin(),
                                          agent_definition.tools.end(),
                                          "*") != agent_definition.tools.end();
            if (!has_wildcard) {
                if (std::find(agent_definition.tools.begin(),
                              agent_definition.tools.end(),
                              tool.name) == agent_definition.tools.end()) {
                    continue;
                }
            }
        }
        
        if (is_async && !async_whitelist.empty()) {
            if (!system_tools.count(tool.name) && !async_whitelist.count(tool.name)) {
                if (agent_definition.tools.empty() ||
                    std::find(agent_definition.tools.begin(),
                              agent_definition.tools.end(),
                              tool.name) == agent_definition.tools.end()) {
                    continue;
                }
            }
        }
        
        if (!allowed_tools.empty()) {
            if (std::find(allowed_tools.begin(), allowed_tools.end(), tool.name) == allowed_tools.end()) {
                continue;
            }
        }
        
        result.push_back(tool);
    }
    
    LOG_INFO("[subagent] tool_filter input={} output={} agent={} async={}",
             base_pool.size(), result.size(), agent_definition.agent_type, is_async ? "true" : "false");
    return result;
}

} // namespace kabot::subagent
