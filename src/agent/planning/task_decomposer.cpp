#include "agent/planning/task_decomposer.hpp"

#include "nlohmann/json.hpp"
#include "utils/logging.hpp"

#include <algorithm>
#include <cctype>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace kabot::agent::planning {

TaskDecomposer::TaskDecomposer(kabot::providers::LLMProvider& provider,
                               int max_tasks_per_plan,
                               int max_tokens)
    : provider_(provider)
    , max_tasks_per_plan_(max_tasks_per_plan)
    , max_tokens_(max_tokens) {}

std::string TaskDecomposer::BuildSystemPrompt(const std::string& project_description) {
    std::string prompt = "You are a project planning assistant. Given a user's instruction, decompose it into a structured list of tasks.\n";
    if (!project_description.empty()) {
        prompt += "\nProject context:\n" + project_description + "\n";
    }
    prompt += "\n"
        "Return ONLY a JSON object with this exact structure:\n"
        "{\n"
        "  \"tasks\": [\n"
        "    {\n"
        "      \"title\": \"Short task title\",\n"
        "      \"instruction\": \"Detailed step-by-step instructions for the task\",\n"
        "      \"priority\": \"normal\",\n"
        "      \"depends_on\": [\"title_of_prerequisite_task_1\", \"title_of_prerequisite_task_2\"],\n"
        "      \"estimated_effort\": \"2 hours\"\n"
        "    }\n"
        "  ]\n"
        "}\n"
        "\n"
        "Rules:\n"
        "- Each task must have a non-empty \"title\" and \"instruction\".\n"
        "- \"priority\" can be \"low\", \"normal\", \"high\", or \"urgent\".\n"
        "- \"depends_on\" is optional and contains titles of other tasks in the same list that must be completed first.\n"
        "- Do not create cyclic dependencies.\n"
        "- Keep the total number of tasks reasonable (preferably under 20).\n";
    return prompt;
}

bool TaskDecomposer::ValidateResponse(const nlohmann::json& json,
                                      int max_tasks,
                                      std::string& error) {
    if (!json.is_object() || !json.contains("tasks") || !json["tasks"].is_array()) {
        error = "expected JSON object with a 'tasks' array";
        return false;
    }
    const auto& tasks = json["tasks"];
    if (tasks.size() > static_cast<std::size_t>(max_tasks)) {
        error = "task list exceeds maximum allowed count (" + std::to_string(max_tasks) + ")";
        return false;
    }
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (!tasks[i].is_object()) {
            error = "task at index " + std::to_string(i) + " is not an object";
            return false;
        }
        if (!tasks[i].contains("title") || !tasks[i]["title"].is_string() ||
            tasks[i]["title"].get<std::string>().empty()) {
            error = "task at index " + std::to_string(i) + " missing non-empty title";
            return false;
        }
        if (!tasks[i].contains("instruction") || !tasks[i]["instruction"].is_string() ||
            tasks[i]["instruction"].get<std::string>().empty()) {
            error = "task at index " + std::to_string(i) + " missing non-empty instruction";
            return false;
        }
    }
    return true;
}

bool TaskDecomposer::DetectCycle(const std::vector<PlannedTask>& tasks, std::string& error) {
    std::unordered_map<std::string, std::size_t> title_to_index;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        title_to_index[tasks[i].title] = i;
    }

    std::vector<std::vector<std::size_t>> graph(tasks.size());
    std::vector<int> in_degree(tasks.size(), 0);

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        for (const auto& dep_title : tasks[i].depends_on) {
            auto it = title_to_index.find(dep_title);
            if (it != title_to_index.end()) {
                graph[it->second].push_back(i);
                ++in_degree[i];
            }
        }
    }

    std::queue<std::size_t> q;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (in_degree[i] == 0) {
            q.push(i);
        }
    }

    std::size_t visited = 0;
    while (!q.empty()) {
        auto u = q.front();
        q.pop();
        ++visited;
        for (auto v : graph[u]) {
            if (--in_degree[v] == 0) {
                q.push(v);
            }
        }
    }

    if (visited != tasks.size()) {
        error = "cyclic dependency detected in task plan";
        return true;
    }
    return false;
}

std::vector<PlannedTask> TaskDecomposer::TopoSort(std::vector<PlannedTask> tasks) {
    std::unordered_map<std::string, std::size_t> title_to_index;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        title_to_index[tasks[i].title] = i;
    }

    std::vector<std::vector<std::size_t>> graph(tasks.size());
    std::vector<int> in_degree(tasks.size(), 0);

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        for (const auto& dep_title : tasks[i].depends_on) {
            auto it = title_to_index.find(dep_title);
            if (it != title_to_index.end()) {
                graph[it->second].push_back(i);
                ++in_degree[i];
            }
        }
    }

    std::queue<std::size_t> q;
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        if (in_degree[i] == 0) {
            q.push(i);
        }
    }

    std::vector<PlannedTask> result;
    result.reserve(tasks.size());
    while (!q.empty()) {
        auto u = q.front();
        q.pop();
        result.push_back(std::move(tasks[u]));
        for (auto v : graph[u]) {
            if (--in_degree[v] == 0) {
                q.push(v);
            }
        }
    }
    return result;
}

namespace {

std::string StripMarkdownFences(std::string content) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    auto first = std::find_if(content.begin(), content.end(), not_space);
    if (first != content.end()) {
        content.erase(content.begin(), first);
    }
    if (content.rfind("```json", 0) == 0) {
        content.erase(0, 7);
    } else if (content.rfind("```", 0) == 0) {
        content.erase(0, 3);
    }
    auto last = std::find_if(content.rbegin(), content.rend(), not_space);
    if (last != content.rend()) {
        content.erase(last.base(), content.end());
    }
    if (content.size() >= 3 && content.compare(content.size() - 3, 3, "```") == 0) {
        content.erase(content.size() - 3);
    }
    auto inner_first = std::find_if(content.begin(), content.end(), not_space);
    if (inner_first != content.begin()) {
        content.erase(content.begin(), inner_first);
    }
    auto inner_last = std::find_if(content.rbegin(), content.rend(), not_space);
    if (inner_last != content.rend() && inner_last.base() != content.end()) {
        content.erase(inner_last.base(), content.end());
    }
    return content;
}

std::string NormalizePriority(const std::string& raw) {
    static const std::unordered_map<std::string, std::string> kMap = {
        {"low", "low"},
        {"normal", "normal"},
        {"medium", "normal"},
        {"high", "high"},
        {"urgent", "high"},
        {"critical", "high"},
        {"important", "high"},
    };
    std::string lower;
    lower.reserve(raw.size());
    for (unsigned char ch : raw) lower.push_back(static_cast<char>(std::tolower(ch)));
    auto it = kMap.find(lower);
    if (it != kMap.end()) return it->second;
    return "normal";
}

}  // namespace

TaskPlan TaskDecomposer::Decompose(const std::string& instruction,
                                   const std::string& project_id,
                                   const std::string& merge_request,
                                   const std::string& project_description) const {
    TaskPlan plan;
    plan.project_id = project_id;
    plan.merge_request = merge_request;

    try {
        std::vector<kabot::providers::Message> messages;
        messages.push_back({"system", BuildSystemPrompt(project_description), {}, {}, {}, {}, {}, false});
        messages.push_back({"user", instruction, {}, {}, {}, {}, {}, false});

        LOG_INFO("[task_decomposer] sending decomposition request to model={}", provider_.GetDefaultModel());
        auto response = provider_.Chat(
            messages,
            {},  // no tools needed for decomposition
            provider_.GetDefaultModel(),
            max_tokens_,
            0.3);
        LOG_INFO("[task_decomposer] received decomposition response content_len={}", response.content.size());

        if (response.content.empty()) {
            LOG_WARN("[task_decomposer] LLM returned empty content for decomposition");
            plan.error = "LLM returned empty decomposition response";
            return plan;
        }

        std::string raw = StripMarkdownFences(response.content);
        auto json = nlohmann::json::parse(raw, nullptr, false);
        if (json.is_discarded()) {
            LOG_WARN("[task_decomposer] failed to parse LLM response as JSON. raw={}", raw);
            plan.error = "failed to parse LLM decomposition response as JSON";
            return plan;
        }

        std::string validation_error;
        if (!ValidateResponse(json, max_tasks_per_plan_, validation_error)) {
            plan.error = validation_error;
            return plan;
        }

        for (const auto& item : json["tasks"]) {
            PlannedTask task;
            task.title = item.value("title", std::string());
            task.instruction = item.value("instruction", std::string());
            task.priority = NormalizePriority(item.value("priority", std::string("normal")));
            task.estimated_effort = item.value("estimated_effort", std::string());
            if (item.contains("depends_on") && item["depends_on"].is_array()) {
                for (const auto& dep : item["depends_on"]) {
                    if (dep.is_string()) {
                        task.depends_on.push_back(dep.get<std::string>());
                    }
                }
            }
            plan.tasks.push_back(std::move(task));
        }

        std::string cycle_error;
        if (DetectCycle(plan.tasks, cycle_error)) {
            plan.error = cycle_error;
            plan.tasks.clear();
            return plan;
        }

        plan.tasks = TopoSort(std::move(plan.tasks));
        plan.success = true;

    } catch (const std::exception& ex) {
        LOG_ERROR("[task_decomposer] decomposition failed: {}", ex.what());
        plan.error = std::string("decomposition failed: ") + ex.what();
    }

    return plan;
}

}  // namespace kabot::agent::planning
