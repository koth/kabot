#include "agent/tools/filesystem.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace kabot::agent::tools {
namespace {

std::string GetParam(const std::unordered_map<std::string, std::string>& params,
                     const std::string& name) {
    auto it = params.find(name);
    if (it == params.end()) {
        return {};
    }
    return it->second;
}

}  // namespace

std::string ReadFileTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})";
}

std::string ReadFileTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    if (path.empty()) {
        return "Error: path is required";
    }
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        return "Error: failed to open file";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string WriteFileTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"]})";
}

std::string WriteFileTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    const auto content = GetParam(params, "content");
    if (path.empty()) {
        return "Error: path is required";
    }
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return "Error: failed to open file";
    }
    file << content;
    return "OK";
}

std::string EditFileTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string"},"patch":{"type":"string"}},"required":["path","patch"]})";
}

std::string EditFileTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    (void)params;
    return "Error: edit_file not implemented";
}

std::string ListDirTool::ParametersJson() const {
    return R"({"type":"object","properties":{"path":{"type":"string"}},"required":["path"]})";
}

std::string ListDirTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto path = GetParam(params, "path");
    if (path.empty()) {
        return "Error: path is required";
    }
    std::filesystem::path root(path);
    if (!std::filesystem::exists(root)) {
        return "Error: path does not exist";
    }
    std::ostringstream oss;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        oss << entry.path().filename().string() << "\n";
    }
    return oss.str();
}

}  // namespace kabot::agent::tools
