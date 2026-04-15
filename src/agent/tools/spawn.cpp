#include "agent/tools/spawn.hpp"

#include "utils/logging.hpp"

namespace kabot::agent::tools {

AgentTool::AgentTool(Spawner spawner)
    : spawner_(std::move(spawner)) {}

std::string AgentTool::Description() const {
    return "Spawn a subagent to handle a task asynchronously or synchronously. "
           "Use this to delegate work to specialized agents.";
}

std::string AgentTool::ParametersJson() const {
    return R"({
        "type": "object",
        "properties": {
            "prompt": {"type": "string", "description": "The task description for the subagent"},
            "subagent_type": {"type": "string", "description": "Agent type: explore, fork, default"},
            "description": {"type": "string", "description": "Short label for the task"},
            "model": {"type": "string", "description": "Override model for this subagent"},
            "run_in_background": {"type": "boolean", "default": false, "description": "Whether to run as a background task"},
            "isolation": {"type": "string", "enum": ["none", "worktree", "remote"], "description": "Execution isolation mode"}
        },
        "required": ["prompt"]
    })";
}

std::string AgentTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    if (!spawner_) {
        return "Error: subagent spawner not available";
    }

    auto it = params.find("prompt");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing prompt parameter";
    }

    kabot::subagent::AgentSpawnInput input;
    input.prompt = it->second;

    if (auto st = params.find("subagent_type"); st != params.end()) {
        input.subagent_type = st->second;
    }
    if (auto d = params.find("description"); d != params.end()) {
        input.description = d->second;
    }
    if (auto m = params.find("model"); m != params.end()) {
        input.model = m->second;
    }
    if (auto bg = params.find("run_in_background"); bg != params.end()) {
        input.run_in_background = (bg->second == "true");
    }
    if (auto iso = params.find("isolation"); iso != params.end()) {
        input.isolation = iso->second;
    }
    if (!session_key_.empty()) {
        input.session_key = session_key_;
    }

    try {
        return spawner_(input);
    } catch (const std::exception& ex) {
        LOG_ERROR("[agent] failed to spawn subagent: {}", ex.what());
        return std::string("Error: ") + ex.what();
    }
}

}  // namespace kabot::agent::tools
