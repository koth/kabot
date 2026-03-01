#include "config/config_schema.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace kabot::config {
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

void ApplyConfigFromJson(Config& config, const nlohmann::json& data) {
    if (!data.is_object()) {
        return;
    }

    if (data.contains("agents") && data["agents"].is_object()) {
        const auto& agents = data["agents"];
        if (agents.contains("defaults") && agents["defaults"].is_object()) {
            const auto& defaults = agents["defaults"];
            if (defaults.contains("workspace") && defaults["workspace"].is_string()) {
                config.agents.defaults.workspace = defaults["workspace"].get<std::string>();
            }
            if (defaults.contains("model") && defaults["model"].is_string()) {
                config.agents.defaults.model = defaults["model"].get<std::string>();
            }
            if (defaults.contains("maxTokens") && defaults["maxTokens"].is_number_integer()) {
                config.agents.defaults.max_tokens = defaults["maxTokens"].get<int>();
            }
            if (defaults.contains("temperature") && defaults["temperature"].is_number()) {
                config.agents.defaults.temperature = defaults["temperature"].get<double>();
            }
            if (defaults.contains("maxToolIterations") && defaults["maxToolIterations"].is_number_integer()) {
                const auto value = defaults["maxToolIterations"].get<int>();
                config.agents.defaults.max_tool_iterations = value;
                config.agents.defaults.max_iterations = value;
            }
        }
    }

    if (data.contains("channels") && data["channels"].is_object()) {
        const auto& channels = data["channels"];
        if (channels.contains("telegram") && channels["telegram"].is_object()) {
            const auto& telegram = channels["telegram"];
            if (telegram.contains("enabled") && telegram["enabled"].is_boolean()) {
                config.channels.telegram.enabled = telegram["enabled"].get<bool>();
            }
            if (telegram.contains("token") && telegram["token"].is_string()) {
                config.channels.telegram.token = telegram["token"].get<std::string>();
            }
            if (telegram.contains("allowFrom") && telegram["allowFrom"].is_array()) {
                config.channels.telegram.allow_from.clear();
                for (const auto& item : telegram["allowFrom"]) {
                    if (item.is_string()) {
                        config.channels.telegram.allow_from.push_back(item.get<std::string>());
                    }
                }
            }
        }
        if (channels.contains("lark") && channels["lark"].is_object()) {
            const auto& lark = channels["lark"];
            if (lark.contains("enabled") && lark["enabled"].is_boolean()) {
                config.channels.lark.enabled = lark["enabled"].get<bool>();
            }
            if (lark.contains("appId") && lark["appId"].is_string()) {
                config.channels.lark.app_id = lark["appId"].get<std::string>();
            }
            if (lark.contains("appSecret") && lark["appSecret"].is_string()) {
                config.channels.lark.app_secret = lark["appSecret"].get<std::string>();
            }
            if (lark.contains("domain") && lark["domain"].is_string()) {
                config.channels.lark.domain = lark["domain"].get<std::string>();
            }
            if (lark.contains("timeoutMs") && lark["timeoutMs"].is_number_integer()) {
                config.channels.lark.timeout_ms = lark["timeoutMs"].get<int>();
            }
            if (lark.contains("allowFrom") && lark["allowFrom"].is_array()) {
                config.channels.lark.allow_from.clear();
                for (const auto& item : lark["allowFrom"]) {
                    if (item.is_string()) {
                        config.channels.lark.allow_from.push_back(item.get<std::string>());
                    }
                }
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
    Config config{};

    const auto config_path = GetConfigPath();
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

    return config;
}

}  // namespace kabot::config
