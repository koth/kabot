#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "agent/tools/tool.hpp"
#include "providers/llm_provider.hpp"

namespace kabot::agent::tools {

class ToolRegistry {
public:
    void Register(std::unique_ptr<Tool> tool);
    Tool* Get(const std::string& name);
    bool Has(const std::string& name) const;
    std::vector<kabot::providers::ToolDefinition> GetDefinitions() const;
    std::string Execute(const std::string& name,
                        const std::unordered_map<std::string, std::string>& params);

    std::vector<std::string> List() const;

private:
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

}  // namespace kabot::agent::tools
