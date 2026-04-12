#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class ReadFileTool : public Tool {
public:
    std::string Name() const override { return "read_file"; }
    std::string Description() const override { return "Read a file from the workspace."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

class WriteFileTool : public Tool {
public:
    std::string Name() const override { return "write_file"; }
    std::string Description() const override { return "Write content to a file."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

class EditFileTool : public Tool {
public:
    std::string Name() const override { return "edit_file"; }
    std::string Description() const override { return "Edit file content (not implemented)."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

class ListDirTool : public Tool {
public:
    std::string Name() const override { return "list_dir"; }
    std::string Description() const override { return "List directory entries."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

class GlobTool : public Tool {
public:
    std::string Name() const override { return "glob"; }
    std::string Description() const override { return "Find files matching a glob pattern."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

class GrepTool : public Tool {
public:
    std::string Name() const override { return "grep"; }
    std::string Description() const override { return "Search file contents with regex."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

}  // namespace kabot::agent::tools
