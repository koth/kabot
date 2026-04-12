#include "agent/tools/shell.hpp"

#include "sandbox/sandbox_executor.hpp"
#include "utils/logging.hpp"

namespace kabot::agent::tools {

BashTool::BashTool(std::string working_dir)
    : working_dir_(std::move(working_dir)) {}

std::string BashTool::ParametersJson() const {
    return R"({"type":"object","properties":{"command":{"type":"string","description":"Shell command to execute"}},"required":["command"]})";
}

std::string BashTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("command");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing command";
    }

    const auto result = kabot::sandbox::SandboxExecutor::Run(
        it->second,
        working_dir_,
        std::chrono::seconds(180));

    if (result.blocked) {
        return result.error.empty() ? "Error: command blocked by policy" : result.error;
    }
    if (result.timed_out) {
        return "Error: command timed out";
    }
    if (!result.output.empty()) {
        LOG_INFO("[bash] stdout\n{}", result.output);
    }
    if (!result.error.empty()) {
        LOG_WARN("[bash] stderr\n{}", result.error);
    }
    if (result.exit_code == 0) {
        LOG_DEBUG("[bash] exit code 0");
        return result.output.empty() ? "(no output)" : result.output;
    }
    if (!result.error.empty()) {
        LOG_WARN("[bash] exit code {}", result.exit_code);
        return "[stderr]\n" + result.error +
               (result.output.empty() ? "" : "\n[stdout]\n" + result.output);
    }
    return result.output.empty() ? "Error: command failed" : result.output;
}

}  // namespace kabot::agent::tools
