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

struct QQBotConfig {
    std::string name = "qqbot";
    bool enabled = false;
    std::string app_id;
    std::string client_secret;
    std::string token;
    bool sandbox = false;
    std::string intents;
    bool skip_tls_verify = false;
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
    QQBotConfig qqbot;
};

struct ChannelsConfig {
    TelegramConfig telegram;
    LarkConfig lark;
    QQBotConfig qqbot;
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

struct RelayConnectionDefaults {
    std::string scheme = "ws";
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string path = "/ws/agent";
    bool use_tls = false;
    bool skip_tls_verify = false;
    int heartbeat_interval_s = 10;
    int reconnect_initial_delay_ms = 1000;
    int reconnect_max_delay_ms = 30000;
};

struct RelayManagedAgentConfig {
    std::string name;
    bool enabled = true;
    std::string local_agent;
    std::string agent_id;
    std::string token;
    std::string scheme;
    std::string host;
    int port = 0;
    std::string path;
    bool use_tls = false;
    bool skip_tls_verify = false;
    int heartbeat_interval_s = 0;
    int reconnect_initial_delay_ms = 0;
    int reconnect_max_delay_ms = 0;
};

struct RelayConfig {
    RelayConnectionDefaults defaults;
    std::vector<RelayManagedAgentConfig> managed_agents;
};

struct HeartbeatConfig {
    bool enabled = true;
    int interval_s = 30 * 60;
    std::string cron_store_path;
    std::string cron_http_host = "0.0.0.0";
    int cron_http_port = 8089;
};

struct TaskSystemConfig {
    bool enabled = false;
    int poll_interval_s = 30;
    int daily_summary_hour_local = 22;
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
    RelayConfig relay;
    ChannelsConfig channels;
    HeartbeatConfig heartbeat;
    TaskSystemConfig task_system;
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

    const RelayManagedAgentConfig* FindRelayManagedAgent(const std::string& name) const {
        for (const auto& relay_agent : relay.managed_agents) {
            if (relay_agent.name == name) {
                return &relay_agent;
            }
        }
        return nullptr;
    }
};

}  // namespace kabot::config
