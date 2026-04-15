#pragma once

#include <string>

#include "agent/subagent/subagent_types.hpp"
#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class AgentTool : public Tool {
public:
    using Spawner = std::function<std::string(const kabot::subagent::AgentSpawnInput&)>;

    explicit AgentTool(Spawner spawner);

    std::string Name() const override { return "agent"; }
    std::string Description() const override;
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

    void SetSessionKey(const std::string& session_key) { session_key_ = session_key; }

private:
    Spawner spawner_;
    std::string session_key_;
};

}  // namespace kabot::agent::tools
