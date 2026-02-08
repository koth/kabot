#include "agent/tools/spawn.hpp"

#include <filesystem>
#include <iostream>
#include <thread>

#include "sandbox/sandbox_executor.hpp"

namespace kabot::agent::tools {

std::string SpawnTool::ParametersJson() const {
    return R"({"type":"object","properties":{"task":{"type":"string"},"label":{"type":"string"}},"required":["task"]})";
}

std::string SpawnTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("task");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing task";
    }
    const auto label_it = params.find("label");
    const auto label = (label_it == params.end() ? std::string("task") : label_it->second);
    const auto task = it->second;
    const auto working_dir = std::filesystem::current_path().string();

    std::thread([task, label, working_dir]() {
        const auto result = kabot::sandbox::SandboxExecutor::Run(
            task,
            working_dir,
            std::chrono::seconds(60));
        std::cerr << "[spawn] label=" << label
                  << " exit=" << result.exit_code
                  << " timeout=" << (result.timed_out ? "true" : "false")
                  << " blocked=" << (result.blocked ? "true" : "false")
                  << " output=\n" << result.output << std::endl;
    }).detach();

    return "Spawned task: " + label;
}

}  // namespace kabot::agent::tools
