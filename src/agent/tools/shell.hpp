#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class BashTool : public Tool {
public:
    explicit BashTool(std::string working_dir);

    std::string Name() const override { return "bash"; }
    std::string Description() const override { return "Execute a shell command in the workspace. Can run read-only commands, builds, tests, git operations, and scripts."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsDestructive() const override { return true; }

private:
    std::string working_dir_;
};

}  // namespace kabot::agent::tools
