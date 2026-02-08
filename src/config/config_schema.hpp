#pragma once

#include <string>
#include <vector>

namespace kabot::config {

struct TelegramConfig {
    bool enabled = false;
    std::string token;
    std::vector<std::string> allow_from;
};

struct ChannelsConfig {
    TelegramConfig telegram;
};

struct ProviderConfig {
    std::string api_key;
    std::string api_base;
};

struct ProvidersConfig {
    ProviderConfig anthropic;
    ProviderConfig openai;
    ProviderConfig openrouter;
    ProviderConfig moonshot;
    ProviderConfig zhipu;
    ProviderConfig vllm;
    ProviderConfig gemini;
    bool use_proxy_for_llm = false;
};

struct AgentDefaults {
    std::string workspace = "~/.kabot/workspace";
    std::string model = "anthropic/claude-opus-4-5";
    std::string brave_api_key;
    int max_iterations = 20;
    int max_tokens = 8192;
    double temperature = 0.7;
    int max_tool_iterations = 20;
};

struct AgentsConfig {
    AgentDefaults defaults;
};

struct HeartbeatConfig {
    bool enabled = true;
    int interval_s = 30 * 60;
    std::string cron_store_path;
};

struct Config {
    AgentsConfig agents;
    ChannelsConfig channels;
    HeartbeatConfig heartbeat;
    ProvidersConfig providers;
};

}  // namespace kabot::config
