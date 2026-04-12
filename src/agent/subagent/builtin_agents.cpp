#include "agent/subagent/builtin_agents.hpp"

namespace kabot::subagent {

const char* ExploreAgentName() { return "explore"; }
const char* ForkAgentName() { return "fork"; }

AgentDefinition ExploreAgent() {
    AgentDefinition def;
    def.agent_type = ExploreAgentName();
    def.when_to_use = "Use when you need to search, explore, or investigate codebases, files, or data without making changes.";
    def.get_system_prompt = []() {
        return "You are an Explore subagent. Your job is to investigate, search, read, and analyze. "
               "Do not make changes to files. Do not execute destructive commands. "
               "Report findings clearly and concisely.";
    };
    def.tools = {"read_file", "list_dir", "glob", "grep", "web_search", "web_fetch", "bash"};
    def.disallowed_tools = {"write_file", "edit_file", "agent", "send_message"};
    def.model = "inherit";
    def.permission_mode = "bubble";
    def.max_turns = 30;
    def.background = true;
    def.memory = "project";
    def.isolation = "none";
    return def;
}

AgentDefinition ForkAgent() {
    AgentDefinition def;
    def.agent_type = ForkAgentName();
    def.when_to_use = "Implicit fork from current context to parallelize reasoning.";
    def.get_system_prompt = []() {
        return "You are a continuation of the parent agent's reasoning. Continue from the provided context.";
    };
    def.tools = {"*"};
    def.model = "inherit";
    def.permission_mode = "bubble";
    def.max_turns = 200;
    def.background = false;
    def.memory = "none";
    def.isolation = "none";
    return def;
}

AgentDefinition DefaultSubagent() {
    AgentDefinition def;
    def.agent_type = "default";
    def.when_to_use = "General-purpose subagent for tasks that should be delegated.";
    def.get_system_prompt = []() {
        return "You are a delegated subagent. Complete the assigned task using the available tools.";
    };
    def.model = "inherit";
    def.permission_mode = "default";
    def.max_turns = 20;
    def.background = false;
    def.memory = "local";
    def.isolation = "none";
    return def;
}

} // namespace kabot::subagent
