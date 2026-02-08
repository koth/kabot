#include "agent/tools/shell.hpp"

#include <iostream>
#include "sandbox/sandbox_executor.hpp"

namespace kabot::agent::tools {

ExecTool::ExecTool(std::string working_dir)
    : working_dir_(std::move(working_dir)) {}

std::string ExecTool::ParametersJson() const {
    return R"({"type":"object","properties":{"command":{"type":"string"}},"required":["command"]})";
}

std::string ExecTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("command");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing command";
    }
    const auto result = kabot::sandbox::SandboxExecutor::Run(
        it->second,
        working_dir_,
        std::chrono::seconds(10));

    if (result.timed_out) {
        return "Error: command timed out";
    }
    if (!result.output.empty()) {
        std::cerr << "[exec] stdout\n" << result.output << std::endl;
    }
    if (!result.error.empty()) {
        std::cerr << "[exec] stderr\n" << result.error << std::endl;
    }
    if (result.exit_code == 0) {
        std::cerr << "[exec] exit code 0" << std::endl;
        return result.output.empty() ? "(no output)" : result.output;
    }
    if (!result.error.empty()) {
        std::cerr << "[exec] exit code " << result.exit_code << std::endl;
        return "[stderr]\n" + result.error +
               (result.output.empty() ? "" : "\n[stdout]\n" + result.output);
    }
    return result.output.empty() ? "Error: command failed" : result.output;
}

}  // namespace kabot::agent::tools
