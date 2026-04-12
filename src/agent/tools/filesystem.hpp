#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class FileReadTool : public Tool {
public:
    std::string Name() const override { return "read_file"; }
    std::string Description() const override { return "Read a file from the workspace. Supports text files, images, and PDFs."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsReadOnly() const override { return true; }
    bool IsConcurrencySafe() const override { return true; }
};

class FileWriteTool : public Tool {
public:
    std::string Name() const override { return "write_file"; }
    std::string Description() const override { return "Write content to a file, creating it or overwriting it."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsDestructive() const override { return true; }
};

class FileEditTool : public Tool {
public:
    std::string Name() const override { return "edit_file"; }
    std::string Description() const override { return "Perform a precise in-place text replacement edit in an existing file."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsDestructive() const override { return true; }
};

class ListDirTool : public Tool {
public:
    std::string Name() const override { return "list_dir"; }
    std::string Description() const override { return "List directory entries."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsReadOnly() const override { return true; }
    bool IsConcurrencySafe() const override { return true; }
};

class GlobTool : public Tool {
public:
    std::string Name() const override { return "glob"; }
    std::string Description() const override { return "Find files by glob pattern (e.g. **/*.cpp)."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsReadOnly() const override { return true; }
    bool IsConcurrencySafe() const override { return true; }
};

class GrepTool : public Tool {
public:
    std::string Name() const override { return "grep"; }
    std::string Description() const override { return "Search file contents by regex pattern."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsReadOnly() const override { return true; }
    bool IsConcurrencySafe() const override { return true; }
};

}  // namespace kabot::agent::tools
