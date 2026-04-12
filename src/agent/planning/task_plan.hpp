#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace kabot::agent::planning {

struct PlannedTask {
    std::string title;
    std::string instruction;
    std::string priority;
    std::vector<std::string> depends_on;
    std::string estimated_effort;
    std::unordered_map<std::string, std::string> metadata;
};

struct TaskPlan {
    std::vector<PlannedTask> tasks;
    std::string project_id;
    std::string merge_request;
    std::string error;
    bool success = false;
};

}  // namespace kabot::agent::planning
