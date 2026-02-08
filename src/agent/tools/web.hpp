#pragma once

#include <string>

#include "agent/tools/tool.hpp"

namespace kabot::agent::tools {

class WebSearchTool : public Tool {
public:
    explicit WebSearchTool(std::string api_key = "");

    std::string Name() const override { return "web_search"; }
    std::string Description() const override { return "Search the web (stub)."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;

private:
    std::string api_key_;
};

class WebFetchTool : public Tool {
public:
    std::string Name() const override { return "web_fetch"; }
    std::string Description() const override { return "Fetch a URL (stub)."; }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

class RedditFetchTool : public Tool {
public:
    std::string Name() const override { return "reddit_fetch"; }
    std::string Description() const override {
        return "Fetch Reddit data (search, subreddit hot, comments) using public JSON endpoints.";
    }
    std::string ParametersJson() const override;
    std::string Execute(const std::unordered_map<std::string, std::string>& params) override;
};

}  // namespace kabot::agent::tools
