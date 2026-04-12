#include "agent/tools/todo.hpp"

#include <sstream>

#include "nlohmann/json.hpp"

namespace kabot::agent::tools {

TodoWriteTool::TodoWriteTool() = default;

std::string TodoWriteTool::ParametersJson() const {
    return R"({
        "type": "object",
        "properties": {
            "todos": {
                "type": "array",
                "description": "List of todo items",
                "items": {
                    "type": "object",
                    "properties": {
                        "id": {"type": "string", "description": "Todo identifier"},
                        "content": {"type": "string", "description": "Todo text"},
                        "status": {"type": "string", "description": "in_progress, done, or pending"}
                    },
                    "required": ["id", "content", "status"]
                }
            }
        },
        "required": ["todos"]
    })";
}

std::string TodoWriteTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("todos");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing todos parameter";
    }
    try {
        nlohmann::json j = nlohmann::json::parse(it->second);
        if (!j.is_array()) {
            return "Error: todos must be an array";
        }
        items_.clear();
        for (const auto& item : j) {
            TodoItem todo;
            todo.id = item.value("id", "");
            todo.content = item.value("content", "");
            todo.status = item.value("status", "pending");
            items_.push_back(todo);
        }
        std::ostringstream oss;
        oss << "Updated " << items_.size() << " todo item(s).";
        return oss.str();
    } catch (const std::exception& ex) {
        return std::string("Error: ") + ex.what();
    }
}

std::vector<TodoWriteTool::TodoItem> TodoWriteTool::Items() const {
    return items_;
}

}  // namespace kabot::agent::tools
