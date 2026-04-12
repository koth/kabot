#include "agent/tools/plan_work.hpp"

#include <sstream>

#include "agent/planning/task_decomposer.hpp"
#include "relay/relay_manager.hpp"
#include "config/config_schema.hpp"
#include "utils/logging.hpp"

namespace kabot::agent::tools {

PlanWorkTool::PlanWorkTool(kabot::providers::LLMProvider& provider,
                           const kabot::config::TaskSystemConfig& task_config,
                           kabot::relay::RelayManager* relay_manager)
    : provider_(provider)
    , task_config_(task_config)
    , relay_manager_(relay_manager) {}

void PlanWorkTool::SetContext(const std::string& channel,
                              const std::string& channel_instance,
                              const std::string& chat_id,
                              const std::string& agent_name) {
    channel_ = channel;
    channel_instance_ = channel_instance;
    chat_id_ = chat_id;
    agent_name_ = agent_name;
}

std::string PlanWorkTool::Description() const {
    return "Decompose a high-level project instruction into a structured list of tasks and optionally submit them to the relay server.";
}

std::string PlanWorkTool::ParametersJson() const {
    return R"({
        "type": "object",
        "properties": {
            "instruction": {
                "type": "string",
                "description": "The high-level project instruction to decompose into tasks"
            },
            "mode": {
                "type": "string",
                "enum": ["plan_only", "plan_and_submit"],
                "description": "plan_only returns the preview; plan_and_submit pushes tasks to relay"
            },
            "project_context": {
                "type": "object",
                "description": "Optional project metadata. If project_id is provided, the tool will attempt to fetch project description from the relay server before decomposition."
            }
        },
        "required": ["instruction"]
    })";
}

std::string PlanWorkTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it_instruction = params.find("instruction");
    if (it_instruction == params.end() || it_instruction->second.empty()) {
        return "Error: missing required 'instruction' parameter";
    }

    const std::string mode = [this, &params]() -> std::string {
        auto it = params.find("mode");
        if (it != params.end() && (it->second == "plan_only" || it->second == "plan_and_submit")) {
            return it->second;
        }
        return task_config_.plan_work_default_mode;
    }();

    std::string project_id;
    std::string merge_request;
    if (auto it = params.find("project_context"); it != params.end() && !it->second.empty()) {
        try {
            auto j = nlohmann::json::parse(it->second);
            if (j.is_object()) {
                if (j.contains("project_id") && j["project_id"].is_string()) {
                    project_id = j["project_id"].get<std::string>();
                }
                if (j.contains("merge_request") && j["merge_request"].is_string()) {
                    merge_request = j["merge_request"].get<std::string>();
                }
            }
        } catch (...) {
            // ignore malformed project_context
        }
    }

    std::string project_description;
    bool project_query_failed = false;
    if (!project_id.empty() && relay_manager_) {
        auto query_result = relay_manager_->QueryProject(project_id);
        if (query_result.success) {
            project_description = query_result.info.description;
        } else {
            project_query_failed = true;
            LOG_WARN("[plan_work] failed to query project {}: {}", project_id, query_result.message);
        }
    }

    kabot::agent::planning::TaskDecomposer decomposer(provider_, task_config_.max_tasks_per_plan);
    auto plan = decomposer.Decompose(it_instruction->second, project_id, merge_request, project_description);

    if (!plan.success) {
        return std::string("Error: ") + plan.error;
    }

    if (mode == "plan_only") {
        std::ostringstream oss;
        if (project_query_failed) {
            oss << "Note: could not fetch project description from relay server. Decomposing based on instruction only.\n\n";
        }
        oss << "Task plan created (" << plan.tasks.size() << " tasks):\n";
        for (std::size_t i = 0; i < plan.tasks.size(); ++i) {
            oss << (i + 1) << ". " << plan.tasks[i].title;
            if (!plan.tasks[i].depends_on.empty()) {
                oss << " [depends on: " << plan.tasks[i].depends_on.size() << " task(s)]";
            }
            oss << "\n";
        }
        return oss.str();
    }

    if (!relay_manager_) {
        return "Error: relay manager not available for task submission";
    }

    std::size_t success_count = 0;
    std::size_t failure_count = 0;
    std::ostringstream summary;
    if (project_query_failed) {
        summary << "Note: could not fetch project description from relay server. Decomposing based on instruction only.\n\n";
    }
    summary << "Submitted tasks:\n";

    for (const auto& task : plan.tasks) {
        kabot::relay::RelayTaskCreate create{};
        create.title = task.title;
        create.instruction = task.instruction;
        create.priority = task.priority;
        create.merge_request = plan.merge_request;

        if (!plan.project_id.empty()) {
            create.project.project_id = plan.project_id;
        }

        if (!channel_.empty()) {
            create.interaction.channel = channel_;
            create.interaction.channel_instance = channel_instance_.empty() ? channel_ : channel_instance_;
            create.interaction.chat_id = chat_id_;
        }

        if (!task.depends_on.empty()) {
            // At creation time we only have local titles, not remote task IDs.
            // We could pass titles as hints in metadata for a later patch.
            create.metadata["depends_on_titles"] = [task]() {
                std::string s;
                for (std::size_t i = 0; i < task.depends_on.size(); ++i) {
                    if (i > 0) s += ",";
                    s += task.depends_on[i];
                }
                return s;
            }();
        }

        // Use project_id as the submission project; if none, we can't submit
        const std::string target_project = plan.project_id.empty() ? "default" : plan.project_id;
        auto result = relay_manager_->SubmitProjectTask(target_project, create);

        if (result.success) {
            ++success_count;
            summary << "[OK] " << task.title;
            if (!result.task_id.empty()) {
                summary << " (id=" << result.task_id << ")";
            }
            summary << "\n";
        } else {
            ++failure_count;
            summary << "[FAIL] " << task.title << ": " << result.message << "\n";
        }
    }

    summary << "\nResult: " << success_count << " succeeded, " << failure_count << " failed.";
    return summary.str();
}

}  // namespace kabot::agent::tools
