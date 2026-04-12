#pragma once

#include <string>
#include <unordered_map>

#include "agent/tools/tool.hpp"

namespace kabot::relay {
class RelayManager;
}

namespace kabot::providers {
class LLMProvider;
}

namespace kabot::config {
struct TaskSystemConfig;
}

namespace kabot::agent::tools {

class PlanWorkTool : public Tool {
public:
    PlanWorkTool(kabot::providers::LLMProvider& provider,
                 const kabot::config::TaskSystemConfig& task_config,
                 kabot::relay::RelayManager* relay_manager);

    void SetContext(const std::string& channel,
                    const std::string& channel_instance,
                    const std::string& chat_id,
                    const std::string& agent_name);
    void SetRelayManager(kabot::relay::RelayManager* relay_manager);

    std::string Name() const override { return "plan_work"; }
    std::string Description() const override;
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

private:
    kabot::providers::LLMProvider& provider_;
    const kabot::config::TaskSystemConfig& task_config_;
    kabot::relay::RelayManager* relay_manager_;
    std::string channel_;
    std::string channel_instance_;
    std::string chat_id_;
    std::string agent_name_;
};

}  // namespace kabot::agent::tools
