#pragma once

#include <string>
#include <vector>

#include "agent/memory_store.hpp"
#include "agent/skills_loader.hpp"
#include "config/config_schema.hpp"
#include "providers/llm_provider.hpp"

namespace kabot::agent {

class ContextBuilder {
public:
    ContextBuilder(std::string workspace, kabot::config::QmdConfig qmd);
    std::string BuildSystemPrompt(const std::vector<std::string>& skill_names = {}) const;
    std::string BuildSystemPrompt(
        const std::vector<std::string>& skill_names,
        const std::string& current_message) const;
    std::vector<kabot::providers::Message> BuildMessages(
        const std::vector<kabot::providers::Message>& history,
        const std::string& current_message,
        const std::vector<std::string>& media);
    std::vector<kabot::providers::Message> AddToolResult(
        std::vector<kabot::providers::Message> messages,
        const std::string& tool_call_id,
        const std::string& tool_name,
        const std::string& result) const;
    std::vector<kabot::providers::Message> AddAssistantMessage(
        std::vector<kabot::providers::Message> messages,
        const std::string& content,
        const std::vector<kabot::providers::ToolCallRequest>& tool_calls) const;

private:
    std::string workspace_;
    MemoryStore memory_;
    SkillsLoader skills_;
    kabot::config::QmdConfig qmd_;

    std::string LoadBootstrapFiles() const;
    std::string BuildQmdContext(const std::string& query) const;
    std::vector<kabot::providers::ContentPart> BuildUserContent(
        const std::string& text,
        const std::vector<std::string>& media) const;
};

}  // namespace kabot::agent
