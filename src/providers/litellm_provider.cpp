#include "providers/litellm_provider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace kabot::providers {
namespace {
std::string HttpLibErrorToString(httplib::Error err) {
    return httplib::to_string(err);
}

struct ParsedUrl {
    bool https = true;
    std::string host;
    int port = 443;
    std::string base_path;
};

ParsedUrl ParseUrl(const std::string& url) {
    ParsedUrl parsed{};
    std::string working = url;
    if (working.rfind("https://", 0) == 0) {
        parsed.https = true;
        working = working.substr(8);
    } else if (working.rfind("http://", 0) == 0) {
        parsed.https = false;
        parsed.port = 80;
        working = working.substr(7);
    }

    const auto slash_pos = working.find('/');
    std::string host_port = working;
    if (slash_pos != std::string::npos) {
        host_port = working.substr(0, slash_pos);
        parsed.base_path = working.substr(slash_pos);
    }

    const auto colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        parsed.host = host_port.substr(0, colon_pos);
        parsed.port = std::stoi(host_port.substr(colon_pos + 1));
    } else {
        parsed.host = host_port;
    }

    if (!parsed.base_path.empty() && parsed.base_path.back() == '/') {
        parsed.base_path.pop_back();
    }

    return parsed;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string GetEnv(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

bool ParseProxyHostPort(const std::string& proxy, std::string& host, int& port) {
    if (proxy.empty()) {
        return false;
    }
    std::string working = proxy;
    const auto scheme_pos = working.find("://");
    if (scheme_pos != std::string::npos) {
        working = working.substr(scheme_pos + 3);
    }
    const auto slash_pos = working.find('/');
    if (slash_pos != std::string::npos) {
        working = working.substr(0, slash_pos);
    }
    const auto colon_pos = working.rfind(':');
    if (colon_pos == std::string::npos) {
        return false;
    }
    host = working.substr(0, colon_pos);
    try {
        port = std::stoi(working.substr(colon_pos + 1));
    } catch (...) {
        return false;
    }
    return !host.empty() && port > 0;
}

std::string MaskKey(const std::string& key) {
    if (key.size() <= 8) {
        return "****";
    }
    return key.substr(0, 4) + "****" + key.substr(key.size() - 4);
}

bool ShouldUseAnthropicMessages(const std::string& model, const std::string& api_base) {
    const auto combined = ToLower(model + " " + api_base);
    return combined.find("kimi") != std::string::npos ||
        combined.find("moonshot") != std::string::npos ||
        combined.find("anthropic") != std::string::npos;
}

nlohmann::json BuildAnthropicContent(
    const kabot::providers::Message& msg) {
    nlohmann::json content = nlohmann::json::array();
    if (!msg.content_parts.empty()) {
        for (const auto& part : msg.content_parts) {
            if (part.type == "text") {
                content.push_back({{"type", "text"}, {"text", part.text}});
            }
        }
    } else if (!msg.content.empty()) {
        content.push_back({{"type", "text"}, {"text", msg.content}});
    }
    return content;
}

nlohmann::json BuildToolInput(const kabot::providers::ToolCallRequest& call) {
    nlohmann::json input = nlohmann::json::object();
    for (const auto& [key, value] : call.arguments) {
        auto parsed = nlohmann::json::parse(value, nullptr, false);
        input[key] = parsed.is_discarded() ? nlohmann::json(value) : parsed;
    }
    return input;
}

std::unordered_map<std::string, std::string> ParseArguments(const nlohmann::json& args) {
    std::unordered_map<std::string, std::string> parsed;
    if (args.is_object()) {
        for (const auto& item : args.items()) {
            if (item.value().is_string()) {
                parsed[item.key()] = item.value().get<std::string>();
            } else {
                parsed[item.key()] = item.value().dump();
            }
        }
        return parsed;
    }

    if (args.is_string()) {
        parsed["raw"] = args.get<std::string>();
    } else {
        parsed["raw"] = args.dump();
    }
    return parsed;
}

}  // namespace

LiteLLMProvider::LiteLLMProvider(std::string api_key,
                                 std::string api_base,
                                 std::string default_model,
                                 bool use_proxy_for_llm)
    : api_key_(std::move(api_key))
    , api_base_(std::move(api_base))
    , default_model_(std::move(default_model))
    , use_proxy_for_llm_(use_proxy_for_llm) {
    is_openrouter_ = (!api_key_.empty() && api_key_.rfind("sk-or-", 0) == 0) ||
        (api_base_.find("openrouter") != std::string::npos);
    is_vllm_ = !api_base_.empty() && !is_openrouter_;
}

std::string LiteLLMProvider::NormalizeModel(
    const std::string& model,
    bool is_openrouter,
    bool is_vllm) {
    std::string normalized = model;
    const auto lower = ToLower(model);

    return normalized;
}

LLMResponse LiteLLMProvider::Chat(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& model,
    int max_tokens,
    double temperature) {
    try {
        const auto chosen_model = NormalizeModel(
            model.empty() ? default_model_ : model,
            is_openrouter_,
            is_vllm_);

        const bool use_anthropic = ShouldUseAnthropicMessages(chosen_model, api_base_);

        nlohmann::json payload;
        if (use_anthropic) {
            std::string system_prompt;
            payload["model"] = chosen_model;
            payload["max_tokens"] = max_tokens;
            payload["temperature"] = temperature;
            payload["messages"] = nlohmann::json::array();

            for (const auto& msg : messages) {
                if (msg.role == "system") {
                    if (!system_prompt.empty()) {
                        system_prompt.append("\n");
                    }
                    if (!msg.content_parts.empty()) {
                        for (const auto& part : msg.content_parts) {
                            if (part.type == "text") {
                                system_prompt.append(part.text);
                            }
                        }
                    } else {
                        system_prompt.append(msg.content);
                    }
                    continue;
                }

                nlohmann::json entry;
                if (msg.role == "tool") {
                    entry["role"] = "user";
                    entry["content"] = nlohmann::json::array({{
                        {"type", "tool_result"},
                        {"tool_use_id", msg.tool_call_id},
                        {"content", msg.content}
                    }});
                } else {
                    entry["role"] = msg.role;
                    auto content = BuildAnthropicContent(msg);
                    if (msg.role == "assistant" && !msg.tool_calls.empty()) {
                        for (const auto& call : msg.tool_calls) {
                            content.push_back({
                                {"type", "tool_use"},
                                {"id", call.id},
                                {"name", call.name},
                                {"input", BuildToolInput(call)}
                            });
                        }
                    }
                    entry["content"] = content;
                }
                payload["messages"].push_back(entry);
            }

            if (!system_prompt.empty()) {
                payload["system"] = system_prompt;
            }

            if (!tools.empty()) {
                nlohmann::json tool_defs = nlohmann::json::array();
                for (const auto& tool : tools) {
                    nlohmann::json params = nlohmann::json::object();
                    if (!tool.parameters_json.empty()) {
                        params = nlohmann::json::parse(tool.parameters_json, nullptr, false);
                        if (params.is_discarded()) {
                            params = nlohmann::json::object();
                        }
                    }
                    tool_defs.push_back({
                        {"name", tool.name},
                        {"description", tool.description},
                        {"input_schema", params}
                    });
                }
                payload["tools"] = tool_defs;
            }
        } else {
            payload["model"] = chosen_model;
            payload["messages"] = nlohmann::json::array();
            payload["max_tokens"] = max_tokens;
            payload["temperature"] = temperature;

            for (const auto& msg : messages) {
                nlohmann::json entry;
                entry["role"] = msg.role;

                if (!msg.name.empty()) {
                    entry["name"] = msg.name;
                }
                if (!msg.tool_call_id.empty()) {
                    entry["tool_call_id"] = msg.tool_call_id;
                }

                if (!msg.content_parts.empty()) {
                    nlohmann::json content = nlohmann::json::array();
                    for (const auto& part : msg.content_parts) {
                        if (part.type == "text") {
                            content.push_back({{"type", "text"}, {"text", part.text}});
                        } else if (part.type == "image_url") {
                            content.push_back({
                                {"type", "image_url"},
                                {"image_url", {{"url", part.image_url}}}
                            });
                        }
                    }
                    entry["content"] = content;
                } else {
                    entry["content"] = msg.content;
                }

                if (!msg.tool_calls.empty()) {
                    nlohmann::json tool_calls = nlohmann::json::array();
                    for (const auto& call : msg.tool_calls) {
                        nlohmann::json args = nlohmann::json::object();
                        for (const auto& [key, value] : call.arguments) {
                            args[key] = value;
                        }
                        tool_calls.push_back({
                            {"id", call.id},
                            {"type", "function"},
                            {"function", {{"name", call.name}, {"arguments", args.dump()}}}
                        });
                    }
                    entry["tool_calls"] = tool_calls;
                }

                payload["messages"].push_back(entry);
            }

            if (!tools.empty()) {
                nlohmann::json tool_defs = nlohmann::json::array();
                for (const auto& tool : tools) {
                    nlohmann::json params = nlohmann::json::object();
                    if (!tool.parameters_json.empty()) {
                        params = nlohmann::json::parse(tool.parameters_json, nullptr, false);
                        if (params.is_discarded()) {
                            params = nlohmann::json::object();
                        }
                    }
                    tool_defs.push_back({
                        {"type", "function"},
                        {"function", {
                            {"name", tool.name},
                            {"description", tool.description},
                            {"parameters", params}
                        }}
                    });
                }
                payload["tools"] = tool_defs;
                payload["tool_choice"] = "auto";
            }
        }

        std::string base_url = api_base_;
        if (base_url.empty()) {
            if (use_anthropic) {
                base_url = "https://api.anthropic.com/v1";
            } else if (chosen_model.rfind("moonshot/", 0) == 0) {
                base_url = "https://api.moonshot.cn/v1";
            } else {
                base_url = is_openrouter_ ? "https://openrouter.ai/api/v1" : "https://api.openai.com/v1";
            }
        }

        auto parsed = ParseUrl(base_url);
        const std::string endpoint = parsed.base_path + (use_anthropic ? "/messages" : "/chat/completions");

        std::string scheme_host_port = parsed.https ? "https://" : "http://";
        scheme_host_port += parsed.host + ":" + std::to_string(parsed.port);
        auto client = std::make_unique<httplib::Client>(scheme_host_port);
        client->set_connection_timeout(60);
        client->set_read_timeout(60);

        if (use_proxy_for_llm_) {
            const auto https_proxy = GetEnv("HTTPS_PROXY");
            const auto http_proxy = GetEnv("HTTP_PROXY");
            const auto all_proxy = GetEnv("ALL_PROXY");
            const auto https_proxy_l = GetEnv("https_proxy");
            const auto http_proxy_l = GetEnv("http_proxy");
            const auto all_proxy_l = GetEnv("all_proxy");
            std::string proxy_host;
            int proxy_port = 0;
            if (!https_proxy.empty() && ParseProxyHostPort(https_proxy, proxy_host, proxy_port)) {
                client->set_proxy(proxy_host, proxy_port);
            } else if (!http_proxy.empty() && ParseProxyHostPort(http_proxy, proxy_host, proxy_port)) {
                client->set_proxy(proxy_host, proxy_port);
            } else if (!https_proxy_l.empty() && ParseProxyHostPort(https_proxy_l, proxy_host, proxy_port)) {
                client->set_proxy(proxy_host, proxy_port);
            } else if (!http_proxy_l.empty() && ParseProxyHostPort(http_proxy_l, proxy_host, proxy_port)) {
                client->set_proxy(proxy_host, proxy_port);
            } else if (!all_proxy.empty() || !all_proxy_l.empty()) {
                std::cerr << "[llm] ALL_PROXY is set but cpp-httplib only supports HTTP proxy" << std::endl;
            }
        }

        std::cerr << "[llm] POST " << scheme_host_port << endpoint
                  << " model=" << chosen_model
                  << " api_key=" << MaskKey(api_key_)
                  << " style=" << (use_anthropic ? "anthropic" : "openai") << std::endl;

        httplib::Headers headers{{"Content-Type", "application/json"}};
        if (!api_key_.empty()) {
            if (use_anthropic) {
                headers.emplace("x-api-key", api_key_);
                headers.emplace("anthropic-version", "2023-06-01");
            } else {
                headers.emplace("Authorization", "Bearer " + api_key_);
            }
        }

        auto response = client->Post(endpoint.c_str(), headers, payload.dump(), "application/json");
        if (!response) {
            const auto err = response.error();
            const auto err_text = HttpLibErrorToString(err);
            std::cerr << "[llm] request failed: httplib error=" << static_cast<int>(err)
                      << "(" << err_text << ")" << std::endl;
            return LLMResponse{
                .content = "Error calling LLM: request failed (httplib error=" + std::to_string(static_cast<int>(err)) +
                    ", " + err_text + ")",
                .finish_reason = "error"};
        }
        if (response->status >= 400) {
            std::cerr << "[llm] HTTP " << response->status
                      << " body=" << response->body << std::endl;
            return LLMResponse{
                .content = "Error calling LLM: HTTP " + std::to_string(response->status),
                .finish_reason = "error"};
        }

        auto json = nlohmann::json::parse(response->body, nullptr, false);
        if (json.is_discarded()) {
            return LLMResponse{.content = "Error calling LLM: invalid response", .finish_reason = "error"};
        }

        LLMResponse parsed_response{};
        if (use_anthropic) {
            if (json.contains("content") && json["content"].is_array()) {
                for (const auto& block : json["content"]) {
                    const auto type = block.value("type", "");
                    if (type == "text") {
                        parsed_response.content += block.value("text", "");
                    } else if (type == "tool_use") {
                        ToolCallRequest call{};
                        call.id = block.value("id", "");
                        call.name = block.value("name", "");
                        if (block.contains("input")) {
                            const auto& input = block["input"];
                            if (input.is_object()) {
                                for (const auto& item : input.items()) {
                                    if (item.value().is_string()) {
                                        call.arguments[item.key()] = item.value().get<std::string>();
                                    } else {
                                        call.arguments[item.key()] = item.value().dump();
                                    }
                                }
                            }
                        }
                        parsed_response.tool_calls.push_back(call);
                    }
                }
            }
            if (json.contains("stop_reason") && json["stop_reason"].is_string()) {
                parsed_response.finish_reason = json["stop_reason"].get<std::string>();
            }
            if (json.contains("usage")) {
                const auto& usage = json["usage"];
                if (usage.contains("input_tokens")) {
                    parsed_response.usage["prompt_tokens"] = usage["input_tokens"].get<int>();
                }
                if (usage.contains("output_tokens")) {
                    parsed_response.usage["completion_tokens"] = usage["output_tokens"].get<int>();
                }
                if (usage.contains("input_tokens") && usage.contains("output_tokens")) {
                    parsed_response.usage["total_tokens"] =
                        usage["input_tokens"].get<int>() + usage["output_tokens"].get<int>();
                }
            }
            return parsed_response;
        }

        if (!json.contains("choices") || json["choices"].empty()) {
            return LLMResponse{.content = "Error calling LLM: invalid response", .finish_reason = "error"};
        }

        const auto& choice = json["choices"][0];
        const auto& message = choice["message"];
        if (message.contains("content") && !message["content"].is_null()) {
            parsed_response.content = message["content"].get<std::string>();
        }

        if (message.contains("tool_calls")) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCallRequest call{};
                call.id = tc.value("id", "");
                if (tc.contains("function")) {
                    call.name = tc["function"].value("name", "");
                    if (tc["function"].contains("arguments")) {
                        auto args = tc["function"]["arguments"];
                        if (args.is_string()) {
                            auto parsed_args = nlohmann::json::parse(args.get<std::string>(), nullptr, false);
                            if (parsed_args.is_discarded()) {
                                call.arguments = ParseArguments(args);
                            } else {
                                call.arguments = ParseArguments(parsed_args);
                            }
                        } else {
                            call.arguments = ParseArguments(args);
                        }
                    }
                }
                parsed_response.tool_calls.push_back(call);
            }
        }

        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
            parsed_response.finish_reason = choice["finish_reason"].get<std::string>();
        }

        if (json.contains("usage")) {
            const auto& usage = json["usage"];
            if (usage.contains("prompt_tokens")) {
                parsed_response.usage["prompt_tokens"] = usage["prompt_tokens"].get<int>();
            }
            if (usage.contains("completion_tokens")) {
                parsed_response.usage["completion_tokens"] = usage["completion_tokens"].get<int>();
            }
            if (usage.contains("total_tokens")) {
                parsed_response.usage["total_tokens"] = usage["total_tokens"].get<int>();
            }
        }

        return parsed_response;
    } catch (const std::exception& ex) {
        return LLMResponse{
            .content = std::string("Error calling LLM: ") + ex.what(),
            .finish_reason = "error"};
    }
}

}  // namespace kabot::providers
