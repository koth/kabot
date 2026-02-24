#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class ExecTool : public Tool {
public:
    explicit ExecTool(std::string working_dir);

    std::string Name() const override { return "exec"; }
    std::string Description() const override { return "Execute a shell command."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

private:
    std::string working_dir_;
};

}  // namespace kabot::agent::tools
