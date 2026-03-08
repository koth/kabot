#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace kabot::config {

struct ChannelBindingConfig {
    std::string agent;
};

struct TelegramConfig {
    std::string name = "telegram";
    bool enabled = false;
    std::string token;
    std::vector<std::string> allow_from;
    ChannelBindingConfig binding;
};

struct LarkConfig {
    std::string name = "lark";
    bool enabled = false;
    std::string app_id;
    std::string app_secret;
    std::string domain;
    int timeout_ms = 10000;
    std::vector<std::string> allow_from;
    ChannelBindingConfig binding;
};

struct ChannelInstanceConfig {
    std::string name;
    std::string type;
    bool enabled = true;
    std::vector<std::string> allow_from;
    ChannelBindingConfig binding;
    TelegramConfig telegram;
    LarkConfig lark;
};

struct ChannelsConfig {
    TelegramConfig telegram;
    LarkConfig lark;
    std::vector<ChannelInstanceConfig> instances;
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
    std::string tool_profile = "full";
    std::string brave_api_key;
    int max_iterations = 20;
    int max_tokens = 8192;
    double temperature = 0.7;
    int max_tool_iterations = 20;
    int max_history_messages = 50;
};

struct AgentInstanceConfig : AgentDefaults {
    std::string name = "default";
};

struct AgentsConfig {
    AgentDefaults defaults;
    std::vector<AgentInstanceConfig> instances;
};

struct HeartbeatConfig {
    bool enabled = true;
    int interval_s = 30 * 60;
    std::string cron_store_path;
    std::string cron_http_host = "0.0.0.0";
    int cron_http_port = 8089;
};

struct QmdConfig {
    bool enabled = false;
    std::string command = "qmd";
    std::string collection;
    std::string index;
    int max_results = 5;
    double min_score = 0.3;
    int timeout_s = 10;
    bool update_on_write = false;
    bool update_embeddings = false;
};

struct LoggingConfig {
    std::string level = "info";
    std::string log_file;
    bool enable_stdout = true;
};

struct Config {
    AgentsConfig agents;
    ChannelsConfig channels;
    HeartbeatConfig heartbeat;
    ProvidersConfig providers;
    QmdConfig qmd;
    LoggingConfig logging;

    const AgentInstanceConfig* FindAgent(const std::string& name) const {
        for (const auto& agent : agents.instances) {
            if (agent.name == name) {
                return &agent;
            }
        }
        return nullptr;
    }

    const ChannelInstanceConfig* FindChannelInstance(const std::string& name) const {
        for (const auto& channel : channels.instances) {
            if (channel.name == name) {
                return &channel;
            }
        }
        return nullptr;
    }
};

}  // namespace kabot::config
