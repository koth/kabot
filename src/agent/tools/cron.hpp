#pragma once

#include <string>
#include <unordered_map>

#include "agent/tools/tool.hpp"
#include "cron/cron_service.hpp"

namespace kabot::agent::tools {

class CronTool : public Tool {
public:
    explicit CronTool(kabot::cron::CronService* cron);

    std::string Name() const override { return "cron"; }
    std::string Description() const override { return "Manage scheduled cron jobs."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

private:
    kabot::cron::CronService* cron_ = nullptr;
};

}  // namespace kabot::agent::tools
