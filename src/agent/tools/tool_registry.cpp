#include "agent/tools/tool_registry.hpp"

#include <iostream>

namespace kabot::agent::tools {

void ToolRegistry::Register(std::unique_ptr<Tool> tool) {
    auto name = tool->Name();
    tools_.emplace(std::move(name), std::move(tool));
}

Tool* ToolRegistry::Get(const std::string& name) {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return nullptr;
    }
    return it->second.get();
}

bool ToolRegistry::Has(const std::string& name) const {
    return tools_.find(name) != tools_.end();
}

std::vector<kabot::providers::ToolDefinition> ToolRegistry::GetDefinitions() const {
    std::vector<kabot::providers::ToolDefinition> defs;
    for (const auto& [name, tool] : tools_) {
        kabot::providers::ToolDefinition def{};
        def.name = name;
        def.description = tool->Description();
        def.parameters_json = tool->ParametersJson();
        defs.push_back(def);
    }
    return defs;
}

std::string ToolRegistry::Execute(
    const std::string& name,
    const std::unordered_map<std::string, std::string>& params) {
    auto tool = Get(name);
    if (!tool) {
        return "Error: Tool '" + name + "' not found";
    }
    std::cerr << "[tool] start name=" << name;
    if (!params.empty()) {
        std::cerr << " params={";
        bool first = true;
        for (const auto& [key, value] : params) {
            if (!first) {
                std::cerr << ", ";
            }
            std::cerr << key << "=" << value;
            first = false;
        }
        std::cerr << "}";
    }
    std::cerr << std::endl;
    const auto result = tool->Execute(params);
    std::cerr << "[tool] end name=" << name << " size=" << result.size() << std::endl;
    return result;
}

std::vector<std::string> ToolRegistry::List() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : tools_) {
        names.push_back(name);
    }
    return names;
}

}  // namespace kabot::agent::tools
