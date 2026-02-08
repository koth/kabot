#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class SpawnTool : public Tool {
public:
    std::string Name() const override { return "spawn"; }
    std::string Description() const override { return "Spawn a background agent (stub)."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

}  // namespace kabot::agent::tools
