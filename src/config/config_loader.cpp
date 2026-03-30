#include "config/config_schema.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "nlohmann/json.hpp"

namespace kabot::config {

Config LoadConfig(const std::filesystem::path& config_path);

namespace {

std::string GetEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string GetEnvFallback(const char* primary, const char* secondary) {
    auto value = GetEnv(primary);
    if (!value.empty()) {
        return value;
    }
    return GetEnv(secondary);
}

std::filesystem::path GetHomePath() {
    const char* home = std::getenv("HOME");
#if defined(_WIN32)
    if (!home) {
        home = std::getenv("USERPROFILE");
    }
#endif
    return std::filesystem::path(home ? home : ".");
}

std::filesystem::path GetConfigPath() {
    return GetHomePath() / ".kabot" / "config.json";
}

void ApplyRuntimeAgentDefaults(AgentInstanceConfig& agent,
                               const AgentDefaults& previous_defaults,
                               const AgentDefaults& current_defaults) {
    if (agent.workspace == previous_defaults.workspace) {
        agent.workspace = current_defaults.workspace;
    }
    if (agent.model == previous_defaults.model) {
        agent.model = current_defaults.model;
    }
    if (agent.tool_profile == previous_defaults.tool_profile) {
        agent.tool_profile = current_defaults.tool_profile;
    }
    if (agent.brave_api_key == previous_defaults.brave_api_key) {
        agent.brave_api_key = current_defaults.brave_api_key;
    }
    if (agent.max_iterations == previous_defaults.max_iterations) {
        agent.max_iterations = current_defaults.max_iterations;
    }
    if (agent.max_tokens == previous_defaults.max_tokens) {
        agent.max_tokens = current_defaults.max_tokens;
    }
    if (agent.temperature == previous_defaults.temperature) {
        agent.temperature = current_defaults.temperature;
    }
    if (agent.max_tool_iterations == previous_defaults.max_tool_iterations) {
        agent.max_tool_iterations = current_defaults.max_tool_iterations;
    }
    if (agent.max_history_messages == previous_defaults.max_history_messages) {
        agent.max_history_messages = current_defaults.max_history_messages;
    }
}

void ApplyProviderConfig(ProviderConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("apiKey") && source["apiKey"].is_string()) {
        target.api_key = source["apiKey"].get<std::string>();
    }
    if (source.contains("apiBase") && source["apiBase"].is_string()) {
        target.api_base = source["apiBase"].get<std::string>();
    }
}

void ApplyAgentDefaults(AgentDefaults& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("workspace") && source["workspace"].is_string()) {
        target.workspace = source["workspace"].get<std::string>();
    }
    if (source.contains("model") && source["model"].is_string()) {
        target.model = source["model"].get<std::string>();
    }
    if (source.contains("toolProfile") && source["toolProfile"].is_string()) {
        target.tool_profile = source["toolProfile"].get<std::string>();
    }
    if (source.contains("maxTokens") && source["maxTokens"].is_number_integer()) {
        target.max_tokens = source["maxTokens"].get<int>();
    }
    if (source.contains("temperature") && source["temperature"].is_number()) {
        target.temperature = source["temperature"].get<double>();
    }
    if (source.contains("maxToolIterations") && source["maxToolIterations"].is_number_integer()) {
        const auto value = source["maxToolIterations"].get<int>();
        target.max_tool_iterations = value;
        target.max_iterations = value;
    }
    if (source.contains("maxIterations") && source["maxIterations"].is_number_integer()) {
        target.max_iterations = source["maxIterations"].get<int>();
    }
    if (source.contains("maxHistoryMessages") && source["maxHistoryMessages"].is_number_integer()) {
        target.max_history_messages = source["maxHistoryMessages"].get<int>();
    }
}

void ApplyRelayConnectionDefaults(RelayConnectionDefaults& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("scheme") && source["scheme"].is_string()) {
        target.scheme = source["scheme"].get<std::string>();
    }
    if (source.contains("host") && source["host"].is_string()) {
        target.host = source["host"].get<std::string>();
    }
    if (source.contains("port") && source["port"].is_number_integer()) {
        target.port = source["port"].get<int>();
    }
    if (source.contains("path") && source["path"].is_string()) {
        target.path = source["path"].get<std::string>();
    }
    if (source.contains("useTls") && source["useTls"].is_boolean()) {
        target.use_tls = source["useTls"].get<bool>();
    }
    if (source.contains("skipTlsVerify") && source["skipTlsVerify"].is_boolean()) {
        target.skip_tls_verify = source["skipTlsVerify"].get<bool>();
    }
    if (source.contains("heartbeatIntervalS") && source["heartbeatIntervalS"].is_number_integer()) {
        target.heartbeat_interval_s = source["heartbeatIntervalS"].get<int>();
    }
    if (source.contains("reconnectInitialDelayMs") && source["reconnectInitialDelayMs"].is_number_integer()) {
        target.reconnect_initial_delay_ms = source["reconnectInitialDelayMs"].get<int>();
    }
    if (source.contains("reconnectMaxDelayMs") && source["reconnectMaxDelayMs"].is_number_integer()) {
        target.reconnect_max_delay_ms = source["reconnectMaxDelayMs"].get<int>();
    }
}

void ApplyRelayManagedAgentConfig(RelayManagedAgentConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("name") && source["name"].is_string()) {
        target.name = source["name"].get<std::string>();
    }
    if (source.contains("enabled") && source["enabled"].is_boolean()) {
        target.enabled = source["enabled"].get<bool>();
    }
    if (source.contains("localAgent") && source["localAgent"].is_string()) {
        target.local_agent = source["localAgent"].get<std::string>();
    } else if (source.contains("agent") && source["agent"].is_string()) {
        target.local_agent = source["agent"].get<std::string>();
    }
    if (source.contains("agentId") && source["agentId"].is_string()) {
        target.agent_id = source["agentId"].get<std::string>();
    }
    if (source.contains("token") && source["token"].is_string()) {
        target.token = source["token"].get<std::string>();
    }
    if (source.contains("scheme") && source["scheme"].is_string()) {
        target.scheme = source["scheme"].get<std::string>();
    }
    if (source.contains("host") && source["host"].is_string()) {
        target.host = source["host"].get<std::string>();
    }
    if (source.contains("port") && source["port"].is_number_integer()) {
        target.port = source["port"].get<int>();
    }
    if (source.contains("path") && source["path"].is_string()) {
        target.path = source["path"].get<std::string>();
    }
    if (source.contains("useTls") && source["useTls"].is_boolean()) {
        target.use_tls = source["useTls"].get<bool>();
    }
    if (source.contains("skipTlsVerify") && source["skipTlsVerify"].is_boolean()) {
        target.skip_tls_verify = source["skipTlsVerify"].get<bool>();
    }
    if (source.contains("heartbeatIntervalS") && source["heartbeatIntervalS"].is_number_integer()) {
        target.heartbeat_interval_s = source["heartbeatIntervalS"].get<int>();
    }
    if (source.contains("reconnectInitialDelayMs") && source["reconnectInitialDelayMs"].is_number_integer()) {
        target.reconnect_initial_delay_ms = source["reconnectInitialDelayMs"].get<int>();
    }
    if (source.contains("reconnectMaxDelayMs") && source["reconnectMaxDelayMs"].is_number_integer()) {
        target.reconnect_max_delay_ms = source["reconnectMaxDelayMs"].get<int>();
    }
}

void ApplyBindingConfig(ChannelBindingConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("agent") && source["agent"].is_string()) {
        target.agent = source["agent"].get<std::string>();
    } else if (source.contains("defaultAgent") && source["defaultAgent"].is_string()) {
        target.agent = source["defaultAgent"].get<std::string>();
    } else if (source.contains("agents") && source["agents"].is_array()) {
        for (const auto& item : source["agents"]) {
            if (item.is_string()) {
                target.agent = item.get<std::string>();
                break;
            }
        }
    }
}

void ApplyTelegramConfig(TelegramConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("name") && source["name"].is_string()) {
        target.name = source["name"].get<std::string>();
    }
    if (source.contains("enabled") && source["enabled"].is_boolean()) {
        target.enabled = source["enabled"].get<bool>();
    }
    if (source.contains("token") && source["token"].is_string()) {
        target.token = source["token"].get<std::string>();
    }
    if (source.contains("allowFrom") && source["allowFrom"].is_array()) {
        target.allow_from.clear();
        for (const auto& item : source["allowFrom"]) {
            if (item.is_string()) {
                target.allow_from.push_back(item.get<std::string>());
            }
        }
    }
    if (source.contains("binding")) {
        ApplyBindingConfig(target.binding, source["binding"]);
    }
}

void ApplyLarkConfig(LarkConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("name") && source["name"].is_string()) {
        target.name = source["name"].get<std::string>();
    }
    if (source.contains("enabled") && source["enabled"].is_boolean()) {
        target.enabled = source["enabled"].get<bool>();
    }
    if (source.contains("appId") && source["appId"].is_string()) {
        target.app_id = source["appId"].get<std::string>();
    }
    if (source.contains("appSecret") && source["appSecret"].is_string()) {
        target.app_secret = source["appSecret"].get<std::string>();
    }
    if (source.contains("domain") && source["domain"].is_string()) {
        target.domain = source["domain"].get<std::string>();
    }
    if (source.contains("timeoutMs") && source["timeoutMs"].is_number_integer()) {
        target.timeout_ms = source["timeoutMs"].get<int>();
    }
    if (source.contains("allowFrom") && source["allowFrom"].is_array()) {
        target.allow_from.clear();
        for (const auto& item : source["allowFrom"]) {
            if (item.is_string()) {
                target.allow_from.push_back(item.get<std::string>());
            }
        }
    }
    if (source.contains("binding")) {
        ApplyBindingConfig(target.binding, source["binding"]);
    }
}

void ApplyQQBotConfig(QQBotConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("name") && source["name"].is_string()) {
        target.name = source["name"].get<std::string>();
    }
    if (source.contains("enabled") && source["enabled"].is_boolean()) {
        target.enabled = source["enabled"].get<bool>();
    }
    if (source.contains("appId") && source["appId"].is_string()) {
        target.app_id = source["appId"].get<std::string>();
    }
    if (source.contains("clientSecret") && source["clientSecret"].is_string()) {
        target.client_secret = source["clientSecret"].get<std::string>();
    }
    if (source.contains("token") && source["token"].is_string()) {
        target.token = source["token"].get<std::string>();
    }
    if (source.contains("sandbox") && source["sandbox"].is_boolean()) {
        target.sandbox = source["sandbox"].get<bool>();
    }
    if (source.contains("intents") && source["intents"].is_string()) {
        target.intents = source["intents"].get<std::string>();
    }
    if (source.contains("skipTlsVerify") && source["skipTlsVerify"].is_boolean()) {
        target.skip_tls_verify = source["skipTlsVerify"].get<bool>();
    }
    if (source.contains("allowFrom") && source["allowFrom"].is_array()) {
        target.allow_from.clear();
        for (const auto& item : source["allowFrom"]) {
            if (item.is_string()) {
                target.allow_from.push_back(item.get<std::string>());
            }
        }
    }
    if (source.contains("binding")) {
        ApplyBindingConfig(target.binding, source["binding"]);
    }
}

void ApplyWeixinConfig(WeixinConfig& target, const nlohmann::json& source) {
    if (!source.is_object()) {
        return;
    }
    if (source.contains("name") && source["name"].is_string()) {
        target.name = source["name"].get<std::string>();
    }
    if (source.contains("enabled") && source["enabled"].is_boolean()) {
        target.enabled = source["enabled"].get<bool>();
    }
    // Note: token is NOT loaded from config file.
    // It is obtained via QR code login and stored in ~/.kabot/
    if (source.contains("accountId") && source["accountId"].is_string()) {
        target.account_id = source["accountId"].get<std::string>();
    }
    if (source.contains("baseUrl") && source["baseUrl"].is_string()) {
        target.base_url = source["baseUrl"].get<std::string>();
    }
    if (source.contains("cdnBaseUrl") && source["cdnBaseUrl"].is_string()) {
        target.cdn_base_url = source["cdnBaseUrl"].get<std::string>();
    }
    if (source.contains("routeTag") && source["routeTag"].is_number_integer()) {
        target.route_tag = source["routeTag"].get<int>();
    }
    if (source.contains("appId") && source["appId"].is_string()) {
        target.app_id = source["appId"].get<std::string>();
    }
    if (source.contains("appVersion") && source["appVersion"].is_string()) {
        target.app_version = source["appVersion"].get<std::string>();
    }
    if (source.contains("allowFrom") && source["allowFrom"].is_array()) {
        target.allow_from.clear();
        for (const auto& item : source["allowFrom"]) {
            if (item.is_string()) {
                target.allow_from.push_back(item.get<std::string>());
            }
        }
    }
    if (source.contains("binding")) {
        ApplyBindingConfig(target.binding, source["binding"]);
    }
}

void NormalizeConfig(Config& config, const AgentDefaults& previous_defaults) {
    if (config.agents.instances.empty()) {
        AgentInstanceConfig agent{};
        static_cast<AgentDefaults&>(agent) = config.agents.defaults;
        agent.name = "default";
        config.agents.instances.push_back(agent);
    }
    for (auto& agent : config.agents.instances) {
        ApplyRuntimeAgentDefaults(agent, previous_defaults, config.agents.defaults);
    }

    if (config.relay.defaults.path.empty()) {
        config.relay.defaults.path = "/ws/agent";
    } else if (config.relay.defaults.path.front() != '/') {
        config.relay.defaults.path = "/" + config.relay.defaults.path;
    }
    if (config.relay.defaults.use_tls) {
        config.relay.defaults.scheme = "wss";
        if (config.relay.defaults.port == 8080) {
            config.relay.defaults.port = 443;
        }
    } else if (config.relay.defaults.scheme.empty()) {
        config.relay.defaults.scheme = "ws";
    }

    for (auto& relay_agent : config.relay.managed_agents) {
        if (relay_agent.local_agent.empty()) {
            relay_agent.local_agent = relay_agent.name;
        }
        if (relay_agent.local_agent.empty() && !config.agents.instances.empty()) {
            relay_agent.local_agent = config.agents.instances.front().name;
        }
        if (relay_agent.scheme.empty()) {
            relay_agent.scheme = config.relay.defaults.scheme;
        }
        if (relay_agent.host.empty()) {
            relay_agent.host = config.relay.defaults.host;
        }
        if (relay_agent.port <= 0) {
            relay_agent.port = config.relay.defaults.port;
        }
        if (relay_agent.path.empty()) {
            relay_agent.path = config.relay.defaults.path;
        } else if (relay_agent.path.front() != '/') {
            relay_agent.path = "/" + relay_agent.path;
        }
        relay_agent.use_tls = relay_agent.use_tls || config.relay.defaults.use_tls || relay_agent.scheme == "wss";
        relay_agent.skip_tls_verify = relay_agent.skip_tls_verify || config.relay.defaults.skip_tls_verify;
        if (relay_agent.use_tls && relay_agent.scheme != "wss") {
            relay_agent.scheme = "wss";
        } else if (!relay_agent.use_tls && relay_agent.scheme.empty()) {
            relay_agent.scheme = "ws";
        }
        if (relay_agent.heartbeat_interval_s <= 0) {
            relay_agent.heartbeat_interval_s = config.relay.defaults.heartbeat_interval_s;
        }
        if (relay_agent.reconnect_initial_delay_ms <= 0) {
            relay_agent.reconnect_initial_delay_ms = config.relay.defaults.reconnect_initial_delay_ms;
        }
        if (relay_agent.reconnect_max_delay_ms <= 0) {
            relay_agent.reconnect_max_delay_ms = config.relay.defaults.reconnect_max_delay_ms;
        }
    }

    if (config.channels.instances.empty()) {
        if (config.channels.telegram.enabled) {
            ChannelInstanceConfig instance{};
            instance.name = config.channels.telegram.name.empty() ? "telegram" : config.channels.telegram.name;
            instance.type = "telegram";
            instance.enabled = config.channels.telegram.enabled;
            instance.allow_from = config.channels.telegram.allow_from;
            instance.binding = config.channels.telegram.binding;
            instance.telegram = config.channels.telegram;
            instance.telegram.name = instance.name;
            config.channels.instances.push_back(instance);
        }
        if (config.channels.lark.enabled) {
            ChannelInstanceConfig instance{};
            instance.name = config.channels.lark.name.empty() ? "lark" : config.channels.lark.name;
            instance.type = "lark";
            instance.enabled = config.channels.lark.enabled;
            instance.allow_from = config.channels.lark.allow_from;
            instance.binding = config.channels.lark.binding;
            instance.lark = config.channels.lark;
            instance.lark.name = instance.name;
            config.channels.instances.push_back(instance);
        }
        if (config.channels.qqbot.enabled) {
            ChannelInstanceConfig instance{};
            instance.name = config.channels.qqbot.name.empty() ? "qqbot" : config.channels.qqbot.name;
            instance.type = "qqbot";
            instance.enabled = config.channels.qqbot.enabled;
            instance.allow_from = config.channels.qqbot.allow_from;
            instance.binding = config.channels.qqbot.binding;
            instance.qqbot = config.channels.qqbot;
            instance.qqbot.name = instance.name;
            config.channels.instances.push_back(instance);
        }
        if (config.channels.weixin.enabled) {
            ChannelInstanceConfig instance{};
            instance.name = config.channels.weixin.name.empty() ? "weixin" : config.channels.weixin.name;
            instance.type = "weixin";
            instance.enabled = config.channels.weixin.enabled;
            instance.allow_from = config.channels.weixin.allow_from;
            instance.binding = config.channels.weixin.binding;
            instance.weixin = config.channels.weixin;
            instance.weixin.name = instance.name;
            config.channels.instances.push_back(instance);
        }
    }

    for (auto& instance : config.channels.instances) {
        if (instance.type == "telegram") {
            if (instance.name.empty()) {
                instance.name = instance.telegram.name.empty() ? "telegram" : instance.telegram.name;
            }
            instance.telegram.name = instance.name;
            instance.allow_from = instance.allow_from.empty() ? instance.telegram.allow_from : instance.allow_from;
            if (instance.binding.agent.empty()) {
                instance.binding = instance.telegram.binding;
            }
            instance.enabled = instance.enabled && !instance.telegram.token.empty();
        } else if (instance.type == "lark") {
            if (instance.name.empty()) {
                instance.name = instance.lark.name.empty() ? "lark" : instance.lark.name;
            }
            instance.lark.name = instance.name;
            instance.allow_from = instance.allow_from.empty() ? instance.lark.allow_from : instance.allow_from;
            if (instance.binding.agent.empty()) {
                instance.binding = instance.lark.binding;
            }
            instance.enabled = instance.enabled && !instance.lark.app_id.empty() && !instance.lark.app_secret.empty();
        } else if (instance.type == "qqbot") {
            if (instance.name.empty()) {
                instance.name = instance.qqbot.name.empty() ? "qqbot" : instance.qqbot.name;
            }
            instance.qqbot.name = instance.name;
            instance.allow_from = instance.allow_from.empty() ? instance.qqbot.allow_from : instance.allow_from;
            if (instance.binding.agent.empty()) {
                instance.binding = instance.qqbot.binding;
            }
            instance.enabled = instance.enabled && !instance.qqbot.app_id.empty()
                && (!instance.qqbot.client_secret.empty() || !instance.qqbot.token.empty());
        } else if (instance.type == "weixin") {
            if (instance.name.empty()) {
                instance.name = instance.weixin.name.empty() ? "weixin" : instance.weixin.name;
            }
            instance.weixin.name = instance.name;
            instance.allow_from = instance.allow_from.empty() ? instance.weixin.allow_from : instance.allow_from;
            if (instance.binding.agent.empty()) {
                instance.binding = instance.weixin.binding;
            }
            // Token is obtained via QR code login and stored in ~/.openclaw/accounts/
            // Enabled if account_id is set (which determines where to load token from)
            instance.enabled = instance.enabled && !instance.weixin.account_id.empty();
        }

        if (instance.binding.agent.empty()) {
            instance.binding.agent = config.agents.instances.front().name;
        }
    }
}

void ApplyConfigFromJson(Config& config, const nlohmann::json& data) {
    if (!data.is_object()) {
        return;
    }

    if (data.contains("tools") && data["tools"].is_object()) {
        const auto& tools = data["tools"];
        if (tools.contains("web") && tools["web"].is_object()) {
            const auto& web = tools["web"];
            if (web.contains("search") && web["search"].is_object()) {
                const auto& search = web["search"];
                if (search.contains("apiKey") && search["apiKey"].is_string()) {
                    config.agents.defaults.brave_api_key = search["apiKey"].get<std::string>();
                }
            }
        }
    }

    if (data.contains("agents") && data["agents"].is_object()) {
        const auto& agents = data["agents"];
        if (agents.contains("defaults") && agents["defaults"].is_object()) {
            ApplyAgentDefaults(config.agents.defaults, agents["defaults"]);
        }
        if (agents.contains("instances") && agents["instances"].is_array()) {
            config.agents.instances.clear();
            for (const auto& item : agents["instances"]) {
                if (!item.is_object()) {
                    continue;
                }
                AgentInstanceConfig agent{};
                static_cast<AgentDefaults&>(agent) = config.agents.defaults;
                if (item.contains("name") && item["name"].is_string()) {
                    agent.name = item["name"].get<std::string>();
                }
                ApplyAgentDefaults(agent, item);
                config.agents.instances.push_back(agent);
            }
        }
    }

    if (data.contains("relay") && data["relay"].is_object()) {
        const auto& relay = data["relay"];
        if (relay.contains("defaults") && relay["defaults"].is_object()) {
            ApplyRelayConnectionDefaults(config.relay.defaults, relay["defaults"]);
        }
        if (relay.contains("managedAgents") && relay["managedAgents"].is_array()) {
            config.relay.managed_agents.clear();
            for (const auto& item : relay["managedAgents"]) {
                if (!item.is_object()) {
                    continue;
                }
                RelayManagedAgentConfig relay_agent{};
                ApplyRelayManagedAgentConfig(relay_agent, item);
                config.relay.managed_agents.push_back(relay_agent);
            }
        }
    }

    if (data.contains("channels") && data["channels"].is_object()) {
        const auto& channels = data["channels"];
        if (channels.contains("telegram") && channels["telegram"].is_object()) {
            ApplyTelegramConfig(config.channels.telegram, channels["telegram"]);
        }
        if (channels.contains("lark") && channels["lark"].is_object()) {
            ApplyLarkConfig(config.channels.lark, channels["lark"]);
        }
        if (channels.contains("qqbot") && channels["qqbot"].is_object()) {
            ApplyQQBotConfig(config.channels.qqbot, channels["qqbot"]);
        }
        if (channels.contains("weixin") && channels["weixin"].is_object()) {
            ApplyWeixinConfig(config.channels.weixin, channels["weixin"]);
        }
        if (channels.contains("instances") && channels["instances"].is_array()) {
            config.channels.instances.clear();
            for (const auto& item : channels["instances"]) {
                if (!item.is_object()) {
                    continue;
                }
                ChannelInstanceConfig instance{};
                if (item.contains("name") && item["name"].is_string()) {
                    instance.name = item["name"].get<std::string>();
                }
                if (item.contains("type") && item["type"].is_string()) {
                    instance.type = item["type"].get<std::string>();
                }
                if (item.contains("enabled") && item["enabled"].is_boolean()) {
                    instance.enabled = item["enabled"].get<bool>();
                }
                if (item.contains("allowFrom") && item["allowFrom"].is_array()) {
                    for (const auto& allow_item : item["allowFrom"]) {
                        if (allow_item.is_string()) {
                            instance.allow_from.push_back(allow_item.get<std::string>());
                        }
                    }
                }
                if (item.contains("binding")) {
                    ApplyBindingConfig(instance.binding, item["binding"]);
                }
                if (instance.type == "telegram") {
                    instance.telegram = config.channels.telegram;
                    ApplyTelegramConfig(instance.telegram, item);
                } else if (instance.type == "lark") {
                    instance.lark = config.channels.lark;
                    ApplyLarkConfig(instance.lark, item);
                } else if (instance.type == "qqbot") {
                    instance.qqbot = config.channels.qqbot;
                    ApplyQQBotConfig(instance.qqbot, item);
                } else if (instance.type == "weixin") {
                    instance.weixin = config.channels.weixin;
                    ApplyWeixinConfig(instance.weixin, item);
                }
                config.channels.instances.push_back(instance);
            }
        }
    }

    if (data.contains("providers") && data["providers"].is_object()) {
        const auto& providers = data["providers"];
        if (providers.contains("useProxyForLLM") && providers["useProxyForLLM"].is_boolean()) {
            config.providers.use_proxy_for_llm = providers["useProxyForLLM"].get<bool>();
        }
        if (providers.contains("anthropic")) {
            ApplyProviderConfig(config.providers.anthropic, providers["anthropic"]);
        }
        if (providers.contains("openai")) {
            ApplyProviderConfig(config.providers.openai, providers["openai"]);
        }
        if (providers.contains("openrouter")) {
            ApplyProviderConfig(config.providers.openrouter, providers["openrouter"]);
        }
        if (providers.contains("moonshot")) {
            ApplyProviderConfig(config.providers.moonshot, providers["moonshot"]);
        }
        if (providers.contains("zhipu")) {
            ApplyProviderConfig(config.providers.zhipu, providers["zhipu"]);
        }
        if (providers.contains("vllm")) {
            ApplyProviderConfig(config.providers.vllm, providers["vllm"]);
        }
        if (providers.contains("gemini")) {
            ApplyProviderConfig(config.providers.gemini, providers["gemini"]);
        }
    }

    if (data.contains("heartbeat") && data["heartbeat"].is_object()) {
        const auto& heartbeat = data["heartbeat"];
        if (heartbeat.contains("enabled") && heartbeat["enabled"].is_boolean()) {
            config.heartbeat.enabled = heartbeat["enabled"].get<bool>();
        }
        if (heartbeat.contains("intervalS") && heartbeat["intervalS"].is_number_integer()) {
            config.heartbeat.interval_s = heartbeat["intervalS"].get<int>();
        }
        if (heartbeat.contains("cronStorePath") && heartbeat["cronStorePath"].is_string()) {
            config.heartbeat.cron_store_path = heartbeat["cronStorePath"].get<std::string>();
        }
        if (heartbeat.contains("cronHttpHost") && heartbeat["cronHttpHost"].is_string()) {
            config.heartbeat.cron_http_host = heartbeat["cronHttpHost"].get<std::string>();
        }
        if (heartbeat.contains("cronHttpPort") && heartbeat["cronHttpPort"].is_number_integer()) {
            config.heartbeat.cron_http_port = heartbeat["cronHttpPort"].get<int>();
        }
    }

    if (data.contains("taskSystem") && data["taskSystem"].is_object()) {
        const auto& task_system = data["taskSystem"];
        if (task_system.contains("enabled") && task_system["enabled"].is_boolean()) {
            config.task_system.enabled = task_system["enabled"].get<bool>();
        }
        if (task_system.contains("pollIntervalS") && task_system["pollIntervalS"].is_number_integer()) {
            config.task_system.poll_interval_s = task_system["pollIntervalS"].get<int>();
        }
        if (task_system.contains("dailySummaryHourLocal") && task_system["dailySummaryHourLocal"].is_number_integer()) {
            config.task_system.daily_summary_hour_local = task_system["dailySummaryHourLocal"].get<int>();
        }
    }

    if (data.contains("qmd") && data["qmd"].is_object()) {
        const auto& qmd = data["qmd"];
        if (qmd.contains("enabled") && qmd["enabled"].is_boolean()) {
            config.qmd.enabled = qmd["enabled"].get<bool>();
        }
        if (qmd.contains("command") && qmd["command"].is_string()) {
            config.qmd.command = qmd["command"].get<std::string>();
        }
        if (qmd.contains("collection") && qmd["collection"].is_string()) {
            config.qmd.collection = qmd["collection"].get<std::string>();
        }
        if (qmd.contains("index") && qmd["index"].is_string()) {
            config.qmd.index = qmd["index"].get<std::string>();
        }
        if (qmd.contains("maxResults") && qmd["maxResults"].is_number_integer()) {
            config.qmd.max_results = qmd["maxResults"].get<int>();
        }
        if (qmd.contains("minScore") && qmd["minScore"].is_number()) {
            config.qmd.min_score = qmd["minScore"].get<double>();
        }
        if (qmd.contains("timeoutS") && qmd["timeoutS"].is_number_integer()) {
            config.qmd.timeout_s = qmd["timeoutS"].get<int>();
        }
        if (qmd.contains("updateOnWrite") && qmd["updateOnWrite"].is_boolean()) {
            config.qmd.update_on_write = qmd["updateOnWrite"].get<bool>();
        }
        if (qmd.contains("updateEmbeddings") && qmd["updateEmbeddings"].is_boolean()) {
            config.qmd.update_embeddings = qmd["updateEmbeddings"].get<bool>();
        }
    }

    if (data.contains("logging") && data["logging"].is_object()) {
        const auto& logging = data["logging"];
        if (logging.contains("level") && logging["level"].is_string()) {
            config.logging.level = logging["level"].get<std::string>();
        }
        if (logging.contains("logFile") && logging["logFile"].is_string()) {
            config.logging.log_file = logging["logFile"].get<std::string>();
        }
        if (logging.contains("enableStdout") && logging["enableStdout"].is_boolean()) {
            config.logging.enable_stdout = logging["enableStdout"].get<bool>();
        }
    }

}

bool ParseBool(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

int ParseInt(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

double ParseDouble(const std::string& value, double fallback) {
    try {
        return std::stod(value);
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> SplitCsv(const std::string& value) {
    std::vector<std::string> items;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

}  // namespace

Config LoadConfig() {
    return LoadConfig(GetConfigPath());
}

Config LoadConfig(const std::filesystem::path& config_path) {
    Config config{};

    if (std::filesystem::exists(config_path)) {
        try {
            std::ifstream input(config_path);
            nlohmann::json data;
            input >> data;
            ApplyConfigFromJson(config, data);
        } catch (...) {
            // Keep defaults on parse errors
        }
    }

    const auto json_agent_defaults = config.agents.defaults;

    const auto telegram_enabled = GetEnv("KABOT_TELEGRAM_ENABLED");
    if (!telegram_enabled.empty()) {
        config.channels.telegram.enabled = ParseBool(telegram_enabled);
    }

    const auto telegram_token = GetEnv("KABOT_TELEGRAM_TOKEN");
    if (!telegram_token.empty()) {
        config.channels.telegram.token = telegram_token;
        config.channels.telegram.enabled = true;
    }

    const auto telegram_allow_from = GetEnv("KABOT_TELEGRAM_ALLOW_FROM");
    if (!telegram_allow_from.empty()) {
        config.channels.telegram.allow_from = SplitCsv(telegram_allow_from);
    }

    const auto lark_enabled = GetEnv("KABOT_LARK_ENABLED");
    if (!lark_enabled.empty()) {
        config.channels.lark.enabled = ParseBool(lark_enabled);
    }

    const auto lark_app_id = GetEnv("KABOT_LARK_APP_ID");
    if (!lark_app_id.empty()) {
        config.channels.lark.app_id = lark_app_id;
        config.channels.lark.enabled = true;
    }

    const auto lark_app_secret = GetEnv("KABOT_LARK_APP_SECRET");
    if (!lark_app_secret.empty()) {
        config.channels.lark.app_secret = lark_app_secret;
        config.channels.lark.enabled = true;
    }

    const auto lark_domain = GetEnv("KABOT_LARK_DOMAIN");
    if (!lark_domain.empty()) {
        config.channels.lark.domain = lark_domain;
    }

    const auto lark_timeout = GetEnv("KABOT_LARK_TIMEOUT_MS");
    if (!lark_timeout.empty()) {
        config.channels.lark.timeout_ms = ParseInt(lark_timeout, config.channels.lark.timeout_ms);
    }

    const auto lark_allow_from = GetEnv("KABOT_LARK_ALLOW_FROM");
    if (!lark_allow_from.empty()) {
        config.channels.lark.allow_from = SplitCsv(lark_allow_from);
    }

    const auto qqbot_enabled = GetEnv("KABOT_QQBOT_ENABLED");
    if (!qqbot_enabled.empty()) {
        config.channels.qqbot.enabled = ParseBool(qqbot_enabled);
    }

    const auto qqbot_app_id = GetEnv("KABOT_QQBOT_APP_ID");
    if (!qqbot_app_id.empty()) {
        config.channels.qqbot.app_id = qqbot_app_id;
        config.channels.qqbot.enabled = true;
    }

    const auto qqbot_client_secret = GetEnv("KABOT_QQBOT_CLIENT_SECRET");
    if (!qqbot_client_secret.empty()) {
        config.channels.qqbot.client_secret = qqbot_client_secret;
        config.channels.qqbot.enabled = true;
    }

    const auto qqbot_token = GetEnv("KABOT_QQBOT_TOKEN");
    if (!qqbot_token.empty()) {
        config.channels.qqbot.token = qqbot_token;
        config.channels.qqbot.enabled = true;
    }

    const auto qqbot_sandbox = GetEnv("KABOT_QQBOT_SANDBOX");
    if (!qqbot_sandbox.empty()) {
        config.channels.qqbot.sandbox = ParseBool(qqbot_sandbox);
    }

    const auto qqbot_intents = GetEnv("KABOT_QQBOT_INTENTS");
    if (!qqbot_intents.empty()) {
        config.channels.qqbot.intents = qqbot_intents;
    }

    const auto qqbot_skip_tls_verify = GetEnv("KABOT_QQBOT_SKIP_TLS_VERIFY");
    if (!qqbot_skip_tls_verify.empty()) {
        config.channels.qqbot.skip_tls_verify = ParseBool(qqbot_skip_tls_verify);
    }

    const auto qqbot_allow_from = GetEnv("KABOT_QQBOT_ALLOW_FROM");
    if (!qqbot_allow_from.empty()) {
        config.channels.qqbot.allow_from = SplitCsv(qqbot_allow_from);
    }

    const auto openrouter_key = GetEnvFallback(
        "KABOT_PROVIDERS__OPENROUTER__API_KEY",
        "KABOT_PROVIDERS_OPENROUTER_API_KEY");
    if (!openrouter_key.empty()) {
        config.providers.openrouter.api_key = openrouter_key;
    }

    const auto openrouter_base = GetEnvFallback(
        "KABOT_PROVIDERS__OPENROUTER__API_BASE",
        "KABOT_PROVIDERS_OPENROUTER_API_BASE");
    if (!openrouter_base.empty()) {
        config.providers.openrouter.api_base = openrouter_base;
    }

    const auto use_proxy_for_llm = GetEnvFallback(
        "KABOT_PROVIDERS__USE_PROXY_FOR_LLM",
        "KABOT_PROVIDERS_USE_PROXY_FOR_LLM");
    if (!use_proxy_for_llm.empty()) {
        config.providers.use_proxy_for_llm = ParseBool(use_proxy_for_llm);
    }

    const auto anthropic_key = GetEnvFallback(
        "KABOT_PROVIDERS__ANTHROPIC__API_KEY",
        "KABOT_PROVIDERS_ANTHROPIC_API_KEY");
    if (!anthropic_key.empty()) {
        config.providers.anthropic.api_key = anthropic_key;
    }

    const auto openai_key = GetEnvFallback(
        "KABOT_PROVIDERS__OPENAI__API_KEY",
        "KABOT_PROVIDERS_OPENAI_API_KEY");
    if (!openai_key.empty()) {
        config.providers.openai.api_key = openai_key;
    }

    const auto moonshot_key = GetEnvFallback(
        "KABOT_PROVIDERS__MOONSHOT__API_KEY",
        "KABOT_PROVIDERS_MOONSHOT_API_KEY");
    if (!moonshot_key.empty()) {
        config.providers.moonshot.api_key = moonshot_key;
    }

    const auto moonshot_base = GetEnvFallback(
        "KABOT_PROVIDERS__MOONSHOT__API_BASE",
        "KABOT_PROVIDERS_MOONSHOT_API_BASE");
    if (!moonshot_base.empty()) {
        config.providers.moonshot.api_base = moonshot_base;
    }

    const auto gemini_key = GetEnvFallback(
        "KABOT_PROVIDERS__GEMINI__API_KEY",
        "KABOT_PROVIDERS_GEMINI_API_KEY");
    if (!gemini_key.empty()) {
        config.providers.gemini.api_key = gemini_key;
    }

    const auto zhipu_key = GetEnvFallback(
        "KABOT_PROVIDERS__ZHIPU__API_KEY",
        "KABOT_PROVIDERS_ZHIPU_API_KEY");
    if (!zhipu_key.empty()) {
        config.providers.zhipu.api_key = zhipu_key;
    }

    const auto zhipu_base = GetEnvFallback(
        "KABOT_PROVIDERS__ZHIPU__API_BASE",
        "KABOT_PROVIDERS_ZHIPU_API_BASE");
    if (!zhipu_base.empty()) {
        config.providers.zhipu.api_base = zhipu_base;
    }

    const auto vllm_key = GetEnvFallback(
        "KABOT_PROVIDERS__VLLM__API_KEY",
        "KABOT_PROVIDERS_VLLM_API_KEY");
    if (!vllm_key.empty()) {
        config.providers.vllm.api_key = vllm_key;
    }

    const auto vllm_base = GetEnvFallback(
        "KABOT_PROVIDERS__VLLM__API_BASE",
        "KABOT_PROVIDERS_VLLM_API_BASE");
    if (!vllm_base.empty()) {
        config.providers.vllm.api_base = vllm_base;
    }

    const auto workspace = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__WORKSPACE",
        "KABOT_AGENT_WORKSPACE");
    if (!workspace.empty()) {
        config.agents.defaults.workspace = workspace;
    }

    const auto agent_model = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__MODEL",
        "KABOT_AGENT_MODEL");
    if (!agent_model.empty()) {
        config.agents.defaults.model = agent_model;
    }

    const auto brave_api_key = GetEnvFallback(
        "KABOT_TOOLS__WEB__SEARCH__API_KEY",
        "KABOT_AGENT_BRAVE_API_KEY");
    if (!brave_api_key.empty()) {
        config.agents.defaults.brave_api_key = brave_api_key;
    }

    const auto max_iterations = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__MAX_ITERATIONS",
        "KABOT_AGENT_MAX_ITERATIONS");
    if (!max_iterations.empty()) {
        config.agents.defaults.max_iterations = ParseInt(
            max_iterations,
            config.agents.defaults.max_iterations);
    }

    const auto max_tokens = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__MAX_TOKENS",
        "KABOT_AGENT_MAX_TOKENS");
    if (!max_tokens.empty()) {
        config.agents.defaults.max_tokens = ParseInt(max_tokens, config.agents.defaults.max_tokens);
    }

    const auto temperature = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__TEMPERATURE",
        "KABOT_AGENT_TEMPERATURE");
    if (!temperature.empty()) {
        config.agents.defaults.temperature = ParseDouble(temperature, config.agents.defaults.temperature);
    }

    const auto max_history_messages = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__MAX_HISTORY_MESSAGES",
        "KABOT_AGENT_MAX_HISTORY_MESSAGES");
    if (!max_history_messages.empty()) {
        config.agents.defaults.max_history_messages = ParseInt(
            max_history_messages,
            config.agents.defaults.max_history_messages);
    }

    const auto qmd_enabled = GetEnvFallback(
        "KABOT_QMD__ENABLED",
        "KABOT_QMD_ENABLED");
    if (!qmd_enabled.empty()) {
        config.qmd.enabled = ParseBool(qmd_enabled);
    }

    const auto qmd_command = GetEnvFallback(
        "KABOT_QMD__COMMAND",
        "KABOT_QMD_COMMAND");
    if (!qmd_command.empty()) {
        config.qmd.command = qmd_command;
    }

    const auto qmd_collection = GetEnvFallback(
        "KABOT_QMD__COLLECTION",
        "KABOT_QMD_COLLECTION");
    if (!qmd_collection.empty()) {
        config.qmd.collection = qmd_collection;
    }

    const auto qmd_index = GetEnvFallback(
        "KABOT_QMD__INDEX",
        "KABOT_QMD_INDEX");
    if (!qmd_index.empty()) {
        config.qmd.index = qmd_index;
    }

    const auto qmd_max_results = GetEnvFallback(
        "KABOT_QMD__MAX_RESULTS",
        "KABOT_QMD_MAX_RESULTS");
    if (!qmd_max_results.empty()) {
        config.qmd.max_results = ParseInt(qmd_max_results, config.qmd.max_results);
    }

    const auto qmd_min_score = GetEnvFallback(
        "KABOT_QMD__MIN_SCORE",
        "KABOT_QMD_MIN_SCORE");
    if (!qmd_min_score.empty()) {
        config.qmd.min_score = ParseDouble(qmd_min_score, config.qmd.min_score);
    }

    const auto qmd_timeout = GetEnvFallback(
        "KABOT_QMD__TIMEOUT_S",
        "KABOT_QMD_TIMEOUT_S");
    if (!qmd_timeout.empty()) {
        config.qmd.timeout_s = ParseInt(qmd_timeout, config.qmd.timeout_s);
    }

    const auto qmd_update_on_write = GetEnvFallback(
        "KABOT_QMD__UPDATE_ON_WRITE",
        "KABOT_QMD_UPDATE_ON_WRITE");
    if (!qmd_update_on_write.empty()) {
        config.qmd.update_on_write = ParseBool(qmd_update_on_write);
    }

    const auto qmd_update_embeddings = GetEnvFallback(
        "KABOT_QMD__UPDATE_EMBEDDINGS",
        "KABOT_QMD_UPDATE_EMBEDDINGS");
    if (!qmd_update_embeddings.empty()) {
        config.qmd.update_embeddings = ParseBool(qmd_update_embeddings);
    }

    const auto max_tool_iterations = GetEnvFallback(
        "KABOT_AGENTS__DEFAULTS__MAX_TOOL_ITERATIONS",
        "KABOT_AGENT_MAX_TOOL_ITERATIONS");
    if (!max_tool_iterations.empty()) {
        const auto value = ParseInt(max_tool_iterations, config.agents.defaults.max_tool_iterations);
        config.agents.defaults.max_tool_iterations = value;
        config.agents.defaults.max_iterations = value;
    }

    const auto heartbeat_enabled = GetEnvFallback(
        "KABOT_HEARTBEAT__ENABLED",
        "KABOT_HEARTBEAT_ENABLED");
    if (!heartbeat_enabled.empty()) {
        config.heartbeat.enabled = ParseBool(heartbeat_enabled);
    }

    const auto heartbeat_interval = GetEnvFallback(
        "KABOT_HEARTBEAT__INTERVAL_S",
        "KABOT_HEARTBEAT_INTERVAL_S");
    if (!heartbeat_interval.empty()) {
        config.heartbeat.interval_s = ParseInt(heartbeat_interval, config.heartbeat.interval_s);
    }

    const auto heartbeat_cron_store = GetEnvFallback(
        "KABOT_HEARTBEAT__CRON_STORE_PATH",
        "KABOT_HEARTBEAT_CRON_STORE_PATH");
    if (!heartbeat_cron_store.empty()) {
        config.heartbeat.cron_store_path = heartbeat_cron_store;
    }

    const auto heartbeat_cron_http_host = GetEnvFallback(
        "KABOT_HEARTBEAT__CRON_HTTP_HOST",
        "KABOT_HEARTBEAT_CRON_HTTP_HOST");
    if (!heartbeat_cron_http_host.empty()) {
        config.heartbeat.cron_http_host = heartbeat_cron_http_host;
    }

    const auto heartbeat_cron_http_port = GetEnvFallback(
        "KABOT_HEARTBEAT__CRON_HTTP_PORT",
        "KABOT_HEARTBEAT_CRON_HTTP_PORT");
    if (!heartbeat_cron_http_port.empty()) {
        config.heartbeat.cron_http_port = ParseInt(
            heartbeat_cron_http_port,
            config.heartbeat.cron_http_port);
    }

    const auto task_system_enabled = GetEnvFallback(
        "KABOT_TASK_SYSTEM__ENABLED",
        "KABOT_TASK_SYSTEM_ENABLED");
    if (!task_system_enabled.empty()) {
        config.task_system.enabled = ParseBool(task_system_enabled);
    }

    const auto task_system_poll_interval = GetEnvFallback(
        "KABOT_TASK_SYSTEM__POLL_INTERVAL_S",
        "KABOT_TASK_SYSTEM_POLL_INTERVAL_S");
    if (!task_system_poll_interval.empty()) {
        config.task_system.poll_interval_s = ParseInt(
            task_system_poll_interval,
            config.task_system.poll_interval_s);
    }

    const auto task_system_daily_summary_hour = GetEnvFallback(
        "KABOT_TASK_SYSTEM__DAILY_SUMMARY_HOUR_LOCAL",
        "KABOT_TASK_SYSTEM_DAILY_SUMMARY_HOUR_LOCAL");
    if (!task_system_daily_summary_hour.empty()) {
        config.task_system.daily_summary_hour_local = ParseInt(
            task_system_daily_summary_hour,
            config.task_system.daily_summary_hour_local);
    }

    NormalizeConfig(config, json_agent_defaults);
    return config;
}

std::vector<std::string> ValidateConfig(const Config& config) {
    std::vector<std::string> errors;
    std::set<std::string> agent_names;
    for (const auto& agent : config.agents.instances) {
        if (agent.name.empty()) {
            errors.push_back("agent instance name must not be empty");
            continue;
        }
        if (!agent_names.insert(agent.name).second) {
            errors.push_back("duplicate agent name: " + agent.name);
        }
        if (agent.tool_profile != "full" && agent.tool_profile != "message_only") {
            errors.push_back("agent instance " + agent.name + " has unsupported toolProfile: " + agent.tool_profile);
        }
    }

    std::set<std::string> channel_names;
    for (const auto& channel : config.channels.instances) {
        if (channel.name.empty()) {
            errors.push_back("channel instance name must not be empty");
        } else if (!channel_names.insert(channel.name).second) {
            errors.push_back("duplicate channel instance name: " + channel.name);
        }
        if (channel.type != "telegram" && channel.type != "lark" && channel.type != "qqbot" && channel.type != "weixin") {
            errors.push_back("unsupported channel type for instance " + channel.name + ": " + channel.type);
        }
        if (channel.type == "qqbot") {
            if (channel.qqbot.app_id.empty()) {
                errors.push_back("qqbot channel instance " + channel.name + " is missing appId");
            }
            if (channel.qqbot.client_secret.empty() && channel.qqbot.token.empty()) {
                errors.push_back("qqbot channel instance " + channel.name + " requires clientSecret or token");
            }
        }
        if (channel.type == "weixin") {
            // Token is obtained via QR code login and stored in ~/.kabot/accounts/
            // account_id determines which account file to load the token from
            if (channel.weixin.account_id.empty()) {
                errors.push_back("weixin channel instance " + channel.name + " is missing account_id");
            }
        }
        if (channel.binding.agent.empty()) {
            errors.push_back("channel instance has no bound agent: " + channel.name);
        } else if (!config.FindAgent(channel.binding.agent)) {
            errors.push_back("channel instance " + channel.name + " references unknown agent: " + channel.binding.agent);
        }
    }

    std::set<std::string> relay_agent_names;
    for (const auto& relay_agent : config.relay.managed_agents) {
        if (relay_agent.name.empty()) {
            errors.push_back("relay managed agent name must not be empty");
            continue;
        }
        if (!relay_agent_names.insert(relay_agent.name).second) {
            errors.push_back("duplicate relay managed agent name: " + relay_agent.name);
        }
        if (relay_agent.local_agent.empty()) {
            errors.push_back("relay managed agent " + relay_agent.name + " has no bound local agent");
        } else if (!config.FindAgent(relay_agent.local_agent)) {
            errors.push_back("relay managed agent " + relay_agent.name + " references unknown local agent: " + relay_agent.local_agent);
        }
        if (relay_agent.agent_id.empty()) {
            errors.push_back("relay managed agent " + relay_agent.name + " is missing agentId");
        }
        if (relay_agent.token.empty()) {
            errors.push_back("relay managed agent " + relay_agent.name + " is missing token");
        }
        if (relay_agent.host.empty()) {
            errors.push_back("relay managed agent " + relay_agent.name + " is missing host");
        }
        if (relay_agent.path.empty() || relay_agent.path.front() != '/') {
            errors.push_back("relay managed agent " + relay_agent.name + " requires path starting with /");
        }
        if (relay_agent.port <= 0) {
            errors.push_back("relay managed agent " + relay_agent.name + " has invalid port");
        }
        if (relay_agent.heartbeat_interval_s <= 0) {
            errors.push_back("relay managed agent " + relay_agent.name + " has invalid heartbeatIntervalS");
        }
        if (relay_agent.reconnect_initial_delay_ms <= 0) {
            errors.push_back("relay managed agent " + relay_agent.name + " has invalid reconnectInitialDelayMs");
        }
        if (relay_agent.reconnect_max_delay_ms < relay_agent.reconnect_initial_delay_ms) {
            errors.push_back("relay managed agent " + relay_agent.name + " has reconnectMaxDelayMs smaller than reconnectInitialDelayMs");
        }
        if (relay_agent.scheme != "ws" && relay_agent.scheme != "wss") {
            errors.push_back("relay managed agent " + relay_agent.name + " has unsupported scheme: " + relay_agent.scheme);
        }
    }
    return errors;
}

}  // namespace kabot::config
