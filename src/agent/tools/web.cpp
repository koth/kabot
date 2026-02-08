#include "agent/tools/web.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace {

struct ParsedUrl {
    bool https = true;
    std::string host;
    int port = 443;
    std::string path;
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
        parsed.path = working.substr(slash_pos);
    } else {
        parsed.path = "/";
    }

    const auto colon_pos = host_port.find(':');
    if (colon_pos != std::string::npos) {
        parsed.host = host_port.substr(0, colon_pos);
        parsed.port = std::stoi(host_port.substr(colon_pos + 1));
    } else {
        parsed.host = host_port;
    }

    return parsed;
}

std::string UrlEncode(const std::string& value) {
    std::ostringstream encoded;
    encoded << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded << c;
        } else if (c == ' ') {
            encoded << "%20";
        } else {
            encoded << '%' << std::setw(2) << std::setfill('0')
                    << static_cast<int>(c);
        }
    }
    return encoded.str();
}

std::string Truncate(const std::string& value, std::size_t max_len) {
    if (value.size() <= max_len) {
        return value;
    }
    return value.substr(0, max_len) + "\n...(truncated)...";
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

void ApplyProxy(httplib::Client& client) {
    const auto https_proxy = GetEnv("HTTPS_PROXY");
    const auto http_proxy = GetEnv("HTTP_PROXY");
    const auto https_proxy_l = GetEnv("https_proxy");
    const auto http_proxy_l = GetEnv("http_proxy");
    std::string host;
    int port = 0;
    if (!https_proxy.empty() && ParseProxyHostPort(https_proxy, host, port)) {
        client.set_proxy(host, port);
    } else if (!http_proxy.empty() && ParseProxyHostPort(http_proxy, host, port)) {
        client.set_proxy(host, port);
    } else if (!https_proxy_l.empty() && ParseProxyHostPort(https_proxy_l, host, port)) {
        client.set_proxy(host, port);
    } else if (!http_proxy_l.empty() && ParseProxyHostPort(http_proxy_l, host, port)) {
        client.set_proxy(host, port);
    }
}

std::string StripHtml(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    bool in_tag = false;
    bool last_space = false;
    for (char ch : input) {
        if (ch == '<') {
            in_tag = true;
            continue;
        }
        if (ch == '>') {
            in_tag = false;
            if (!last_space) {
                output.push_back(' ');
                last_space = true;
            }
            continue;
        }
        if (in_tag) {
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!last_space) {
                output.push_back(' ');
                last_space = true;
            }
        } else {
            output.push_back(ch);
            last_space = false;
        }
    }
    return output;
}

std::string NormalizeSubreddit(const std::string& input) {
    std::string name = input;
    while (!name.empty() && name.front() == '/') {
        name.erase(0, 1);
    }
    if (name.rfind("r/", 0) == 0) {
        name.erase(0, 2);
    }
    return name;
}

std::string ExtractPostId(const std::string& input) {
    if (input.empty()) {
        return {};
    }
    auto pos = input.find("comments/");
    if (pos != std::string::npos) {
        pos += 9;
        auto end = input.find_first_of("/?", pos);
        return input.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
    pos = input.find("redd.it/");
    if (pos != std::string::npos) {
        pos += 8;
        auto end = input.find_first_of("/?", pos);
        return input.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
    auto end = input.find_first_of("/?");
    return input.substr(0, end == std::string::npos ? std::string::npos : end);
}

}

namespace kabot::agent::tools {

WebSearchTool::WebSearchTool(std::string api_key)
    : api_key_(std::move(api_key)) {}

std::string WebSearchTool::ParametersJson() const {
    return R"({"type":"object","properties":{"query":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":10}},"required":["query"]})";
}

std::string WebSearchTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("query");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing query";
    }
    if (api_key_.empty()) {
        return "Error: missing Brave API key";
    }

    int limit = 5;
    if (auto limit_it = params.find("limit"); limit_it != params.end()) {
        try {
            limit = std::clamp(std::stoi(limit_it->second), 1, 10);
        } catch (...) {
            limit = 5;
        }
    }
    const auto encoded = UrlEncode(it->second);
    const std::string base = "https://api.search.brave.com";
    const std::string path = "/res/v1/web/search?q=" + encoded + "&source=web";
    auto parsed = ParseUrl(base);

    std::string scheme_host_port = parsed.https ? "https://" : "http://";
    scheme_host_port += parsed.host + ":" + std::to_string(parsed.port);
    httplib::Client client(scheme_host_port);
    client.set_connection_timeout(15);
    client.set_read_timeout(15);
    ApplyProxy(client);

    httplib::Headers headers{{"Accept", "application/json"},
                             {"X-Subscription-Token", api_key_}};
    auto response = client.Get(path.c_str(), headers);
    if (!response) {
        return "Error: web_search request failed";
    }
    if (response->status >= 400) {
        return "Error: web_search HTTP " + std::to_string(response->status);
    }

    auto json = nlohmann::json::parse(response->body, nullptr, false);
    if (json.is_discarded()) {
        return "Error: web_search invalid response";
    }

    std::ostringstream oss;
    if (json.contains("web") && json["web"].contains("results")) {
        const auto& results = json["web"]["results"];
        int count = 0;
        for (const auto& item : results) {
            if (count >= limit) {
                break;
            }
            const auto title = item.value("title", "");
            const auto url = item.value("url", "");
            const auto desc = item.value("description", "");
            if (title.empty() && url.empty()) {
                continue;
            }
            oss << "- " << title << "\n  " << url;
            if (!desc.empty()) {
                oss << "\n  " << desc;
            }
            oss << "\n";
            count++;
        }
    }

    const auto output = oss.str();
    return output.empty() ? "No results" : output;
}

std::string WebFetchTool::ParametersJson() const {
    return R"({"type":"object","properties":{"url":{"type":"string"},"maxBytes":{"type":"integer","minimum":1024,"maximum":20000},"textOnly":{"type":"boolean"}},"required":["url"]})";
}

std::string WebFetchTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    auto it = params.find("url");
    if (it == params.end() || it->second.empty()) {
        return "Error: missing url";
    }

    std::size_t max_bytes = 8000;
    if (auto max_it = params.find("maxBytes"); max_it != params.end()) {
        try {
            const auto value = std::stoul(max_it->second);
            max_bytes = std::clamp<std::size_t>(value, 1024, 20000);
        } catch (...) {
            max_bytes = 8000;
        }
    }
    bool text_only = false;
    if (auto text_it = params.find("textOnly"); text_it != params.end()) {
        const auto value = text_it->second;
        text_only = (value == "true" || value == "1" || value == "yes");
    }

    auto parsed = ParseUrl(it->second);
    if (parsed.host.empty()) {
        return "Error: invalid url";
    }

    std::string scheme_host_port = parsed.https ? "https://" : "http://";
    scheme_host_port += parsed.host + ":" + std::to_string(parsed.port);
    httplib::Client client(scheme_host_port);
    client.set_connection_timeout(20);
    client.set_read_timeout(20);
    ApplyProxy(client);

    auto response = client.Get(parsed.path.c_str());
    if (!response) {
        return "Error: web_fetch request failed";
    }
    if (response->status >= 400) {
        return "Error: web_fetch HTTP " + std::to_string(response->status);
    }
    std::string body = response->body;
    if (text_only) {
        body = StripHtml(body);
    }
    return Truncate(body, max_bytes);
}

std::string RedditFetchTool::ParametersJson() const {
    return R"({"type":"object","properties":{"mode":{"type":"string","enum":["search","subreddit_hot","comments"]},"query":{"type":"string"},"subreddit":{"type":"string"},"postId":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":50},"sort":{"type":"string","enum":["hot","new","top","rising","relevance","comments"]}},"required":["mode"]})";
}

std::string RedditFetchTool::Execute(const std::unordered_map<std::string, std::string>& params) {
    const auto mode_it = params.find("mode");
    if (mode_it == params.end() || mode_it->second.empty()) {
        return "Error: missing mode";
    }
    const auto mode = mode_it->second;

    int limit = 5;
    if (auto limit_it = params.find("limit"); limit_it != params.end()) {
        try {
            limit = std::clamp(std::stoi(limit_it->second), 1, 50);
        } catch (...) {
            limit = 5;
        }
    }

    std::string path;
    if (mode == "search") {
        auto query_it = params.find("query");
        if (query_it == params.end() || query_it->second.empty()) {
            return "Error: missing query";
        }
        std::string sort = "relevance";
        if (auto sort_it = params.find("sort"); sort_it != params.end() && !sort_it->second.empty()) {
            sort = sort_it->second;
        }
        path = "/search.json?q=" + UrlEncode(query_it->second) + "&limit=" + std::to_string(limit) +
               "&sort=" + UrlEncode(sort);
    } else if (mode == "subreddit_hot") {
        auto sub_it = params.find("subreddit");
        if (sub_it == params.end() || sub_it->second.empty()) {
            return "Error: missing subreddit";
        }
        std::string sort = "hot";
        if (auto sort_it = params.find("sort"); sort_it != params.end() && !sort_it->second.empty()) {
            sort = sort_it->second;
        }
        const auto subreddit = NormalizeSubreddit(sub_it->second);
        if (subreddit.empty()) {
            return "Error: invalid subreddit";
        }
        path = "/r/" + UrlEncode(subreddit) + "/" + UrlEncode(sort) + ".json?limit=" +
               std::to_string(limit);
    } else if (mode == "comments") {
        auto post_it = params.find("postId");
        if (post_it == params.end() || post_it->second.empty()) {
            return "Error: missing postId";
        }
        const auto post_id = ExtractPostId(post_it->second);
        if (post_id.empty()) {
            return "Error: invalid postId";
        }
        path = "/comments/" + UrlEncode(post_id) + ".json?limit=" + std::to_string(limit);
    } else {
        return "Error: unsupported mode";
    }

    const std::string base = "https://www.reddit.com";
    auto parsed = ParseUrl(base);
    std::string scheme_host_port = parsed.https ? "https://" : "http://";
    scheme_host_port += parsed.host + ":" + std::to_string(parsed.port);
    httplib::Client client(scheme_host_port);
    client.set_connection_timeout(20);
    client.set_read_timeout(20);
    ApplyProxy(client);

    httplib::Headers headers{{"Accept", "application/json"}, {"User-Agent", "kabot/1.0"}};
    auto response = client.Get(path.c_str(), headers);
    if (!response) {
        return "Error: reddit_fetch request failed";
    }
    if (response->status >= 400) {
        return "Error: reddit_fetch HTTP " + std::to_string(response->status);
    }

    auto json = nlohmann::json::parse(response->body, nullptr, false);
    if (json.is_discarded()) {
        return "Error: reddit_fetch invalid response";
    }

    nlohmann::json output = nlohmann::json::object();
    if (mode == "comments") {
        if (!json.is_array() || json.size() < 2) {
            return "Error: reddit_fetch invalid comments response";
        }
        nlohmann::json post = nlohmann::json::object();
        if (json[0].contains("data") && json[0]["data"].contains("children") &&
            !json[0]["data"]["children"].empty()) {
            const auto& post_data = json[0]["data"]["children"][0]["data"];
            post = {
                {"id", post_data.value("id", "")},
                {"title", post_data.value("title", "")},
                {"author", post_data.value("author", "")},
                {"subreddit", post_data.value("subreddit", "")},
                {"score", post_data.value("score", 0)},
                {"num_comments", post_data.value("num_comments", 0)},
                {"url", post_data.value("url", "")},
                {"permalink", post_data.value("permalink", "")},
                {"created_utc", post_data.value("created_utc", 0.0)}
            };
        }

        nlohmann::json comments = nlohmann::json::array();
        if (json[1].contains("data") && json[1]["data"].contains("children")) {
            for (const auto& child : json[1]["data"]["children"]) {
                if (comments.size() >= static_cast<std::size_t>(limit)) {
                    break;
                }
                if (!child.contains("kind") || child["kind"] != "t1") {
                    continue;
                }
                const auto& data = child["data"];
                comments.push_back({
                    {"id", data.value("id", "")},
                    {"author", data.value("author", "")},
                    {"body", data.value("body", "")},
                    {"score", data.value("score", 0)},
                    {"permalink", data.value("permalink", "")},
                    {"created_utc", data.value("created_utc", 0.0)},
                    {"depth", data.value("depth", 0)}
                });
            }
        }
        output["post"] = post;
        output["comments"] = comments;
        output["count"] = comments.size();
        return output.dump(2);
    }

    nlohmann::json items = nlohmann::json::array();
    if (json.contains("data") && json["data"].contains("children")) {
        for (const auto& child : json["data"]["children"]) {
            if (items.size() >= static_cast<std::size_t>(limit)) {
                break;
            }
            const auto& data = child["data"];
            items.push_back({
                {"id", data.value("id", "")},
                {"title", data.value("title", "")},
                {"author", data.value("author", "")},
                {"subreddit", data.value("subreddit", "")},
                {"score", data.value("score", 0)},
                {"num_comments", data.value("num_comments", 0)},
                {"url", data.value("url", "")},
                {"permalink", data.value("permalink", "")},
                {"created_utc", data.value("created_utc", 0.0)}
            });
        }
    }
    output["items"] = items;
    output["count"] = items.size();
    return output.dump(2);
}

}  // namespace kabot::agent::tools
