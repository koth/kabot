#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/config_schema.hpp"

namespace kabot::providers {

struct ToolDefinition {
    std::string name;
    std::string description;
    std::string parameters_json;
};

struct ContentPart {
    std::string type;
    std::string text;
    std::string image_url;
};

struct ToolCallRequest {
    std::string id;
    std::string name;
    std::unordered_map<std::string, std::string> arguments;
};

struct Message {
    std::string role;
    std::string content;
    std::string name;
    std::string tool_call_id;
    std::vector<ToolCallRequest> tool_calls;
    std::vector<ContentPart> content_parts;
};

struct LLMResponse {
    std::string content;
    std::vector<ToolCallRequest> tool_calls;
    std::string finish_reason = "stop";
    std::unordered_map<std::string, int> usage;

    bool HasToolCalls() const { return !tool_calls.empty(); }
};

struct ProviderSettings {
    std::string api_key;
    std::string api_base;
    std::string model;
    bool use_proxy_for_llm = false;
};

class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    virtual LLMResponse Chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools,
        const std::string& model,
        int max_tokens,
        double temperature) = 0;
    virtual std::string GetDefaultModel() const = 0;
};

ProviderSettings ResolveProviderSettings(const kabot::config::Config& config);
std::unique_ptr<LLMProvider> CreateProvider(const kabot::config::Config& config);

}  // namespace kabot::providers
