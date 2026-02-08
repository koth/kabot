#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "providers/llm_provider.hpp"

namespace kabot::providers {

class LiteLLMProvider : public LLMProvider {
public:
    LiteLLMProvider(std::string api_key,
                    std::string api_base,
                    std::string default_model,
                    bool use_proxy_for_llm);

    LLMResponse Chat(
        const std::vector<Message>& messages,
        const std::vector<ToolDefinition>& tools,
        const std::string& model,
        int max_tokens,
        double temperature) override;

    std::string GetDefaultModel() const override { return default_model_; }

private:
    std::string api_key_;
    std::string api_base_;
    std::string default_model_;
    bool is_openrouter_ = false;
    bool is_vllm_ = false;
    bool use_proxy_for_llm_ = false;

    static std::string NormalizeModel(
        const std::string& model,
        bool is_openrouter,
        bool is_vllm);
};

}  // namespace kabot::providers
