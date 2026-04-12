#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class TodoWriteTool : public Tool {
public:
    TodoWriteTool();

    std::string Name() const override { return "todo_write"; }
    std::string Description() const override { return "Update the todo/task list shown to the user. Provides structured task tracking."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
    bool IsDestructive() const override { return true; }

    struct TodoItem {
        std::string id;
        std::string content;
        std::string status;
    };

    std::vector<TodoItem> Items() const;

private:
    mutable std::vector<TodoItem> items_;
};

}  // namespace kabot::agent::tools
