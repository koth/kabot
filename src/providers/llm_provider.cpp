#include "providers/llm_provider.hpp"

#include "providers/litellm_provider.hpp"

namespace kabot::providers {

ProviderSettings ResolveProviderSettings(const kabot::config::Config& config) {
    ProviderSettings settings{};
    settings.model = config.agents.defaults.model.empty()
        ? "anthropic/claude-opus-4-5"
        : config.agents.defaults.model;
    settings.use_proxy_for_llm = config.providers.use_proxy_for_llm;

    if (!config.providers.openrouter.api_key.empty()) {
        settings.api_key = config.providers.openrouter.api_key;
        settings.api_base = config.providers.openrouter.api_base.empty()
            ? "https://openrouter.ai/api/v1"
            : config.providers.openrouter.api_base;
        return settings;
    }

    if (!config.providers.moonshot.api_key.empty()) {
        settings.api_key = config.providers.moonshot.api_key;
        settings.api_base = config.providers.moonshot.api_base.empty()
            ? "https://api.moonshot.cn/v1"
            : config.providers.moonshot.api_base;
        return settings;
    }

    if (!config.providers.anthropic.api_key.empty()) {
        settings.api_key = config.providers.anthropic.api_key;
        return settings;
    }

    if (!config.providers.openai.api_key.empty()) {
        settings.api_key = config.providers.openai.api_key;
        return settings;
    }

    if (!config.providers.gemini.api_key.empty()) {
        settings.api_key = config.providers.gemini.api_key;
        return settings;
    }

    if (!config.providers.zhipu.api_key.empty()) {
        settings.api_key = config.providers.zhipu.api_key;
        settings.api_base = config.providers.zhipu.api_base;
        return settings;
    }

    if (!config.providers.vllm.api_key.empty() || !config.providers.vllm.api_base.empty()) {
        settings.api_key = config.providers.vllm.api_key;
        settings.api_base = config.providers.vllm.api_base;
        return settings;
    }

    return settings;
}

std::unique_ptr<LLMProvider> CreateProvider(const kabot::config::Config& config) {
    const auto settings = ResolveProviderSettings(config);
    return std::make_unique<LiteLLMProvider>(
        settings.api_key,
        settings.api_base,
        settings.model,
        settings.use_proxy_for_llm);
}

}  // namespace kabot::providers
