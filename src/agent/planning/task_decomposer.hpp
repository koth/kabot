#pragma once

#include "agent/planning/task_plan.hpp"
#include "providers/llm_provider.hpp"
#include "nlohmann/json.hpp"

namespace kabot::agent::planning {

class TaskDecomposer {
public:
    explicit TaskDecomposer(kabot::providers::LLMProvider& provider,
                            int max_tasks_per_plan = 20);

    TaskPlan Decompose(const std::string& instruction,
                       const std::string& project_id = {},
                       const std::string& merge_request = {}) const;

    // Exposed for unit testing
    static bool ValidateResponse(const nlohmann::json& json, int max_tasks, std::string& error);
    static bool DetectCycle(const std::vector<PlannedTask>& tasks, std::string& error);
    static std::vector<PlannedTask> TopoSort(std::vector<PlannedTask> tasks);

private:
    kabot::providers::LLMProvider& provider_;
    int max_tasks_per_plan_;

    static std::string BuildSystemPrompt();
};

}  // namespace kabot::agent::planning
