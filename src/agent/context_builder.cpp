#include "agent/context_builder.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <utility>

#include "sandbox/sandbox_executor.hpp"

namespace kabot::agent {

ContextBuilder::ContextBuilder(std::string workspace, kabot::config::QmdConfig qmd)
    : workspace_(std::move(workspace))
    , memory_(workspace_)
    , skills_(workspace_)
    , qmd_(std::move(qmd)) {}

std::string ContextBuilder::BuildSystemPrompt(const std::vector<std::string>& skill_names) const {
    return BuildSystemPrompt(skill_names, "");
}

std::string ContextBuilder::BuildSystemPrompt(
    const std::vector<std::string>& skill_names,
    const std::string& current_message) const {
    std::ostringstream oss;
    oss << "# kabot\n\n";
    oss << "## Workspace\n";
    oss << "Your workspace is at: " << workspace_ << "\n\n";

    const auto bootstrap = LoadBootstrapFiles();
    if (!bootstrap.empty()) {
        oss << bootstrap << "\n\n";
    }

    std::string memory;
    if (qmd_.enabled) {
        std::cerr << "[context] memory=QMD" << std::endl;
        if (!current_message.empty()) {
            std::cerr << "[context] qmd_query=" << current_message << std::endl;
            memory = BuildQmdContext(current_message);
            if (!memory.empty()) {
                std::cerr << "[context] qmd_memory\n" << memory << std::endl;
            }
        }
    } else {
        std::cerr << "[context] memory=FULL" << std::endl;
        memory = memory_.GetMemoryContext();
    }
    if (!memory.empty()) {
        oss << "# Memory\n\n" << memory << "\n\n";
    }

    auto active_names = skill_names;
    if (active_names.empty()) {
        active_names = skills_.GetAlwaysSkills();
    }
    const auto active_skills = skills_.LoadSkillsForContext(active_names);
    if (!active_skills.empty()) {
        oss << "# Active Skills\n\n" << active_skills << "\n\n";
    }

    const auto summary = skills_.BuildSkillsSummary();
    if (!summary.empty()) {
        oss << "# Skills\n\n";
        oss << "The following skills extend your capabilities. To use a skill, read its SKILL.md file using the read_file tool.\n\n";
        oss << summary << "\n";
    }

    return oss.str();
}

std::vector<kabot::providers::Message> ContextBuilder::BuildMessages(
    const std::vector<kabot::providers::Message>& history,
    const std::string& current_message,
    const std::vector<std::string>& media) {
    std::vector<kabot::providers::Message> messages;
    kabot::providers::Message system_message{};
    system_message.role = "system";
    system_message.content = BuildSystemPrompt({}, current_message);
    messages.push_back(system_message);

    messages.insert(messages.end(), history.begin(), history.end());

    kabot::providers::Message user_message{};
    user_message.role = "user";
    user_message.content_parts = BuildUserContent(current_message, media);
    if (user_message.content_parts.empty()) {
        user_message.content = current_message;
    }
    messages.push_back(user_message);
    return messages;
}

std::vector<kabot::providers::Message> ContextBuilder::AddToolResult(
    std::vector<kabot::providers::Message> messages,
    const std::string& tool_call_id,
    const std::string& tool_name,
    const std::string& result) const {
    kabot::providers::Message tool_message{};
    tool_message.role = "tool";
    tool_message.tool_call_id = tool_call_id;
    tool_message.name = tool_name;
    tool_message.content = result;
    messages.push_back(tool_message);
    return messages;
}

std::vector<kabot::providers::Message> ContextBuilder::AddAssistantMessage(
    std::vector<kabot::providers::Message> messages,
    const std::string& content,
    const std::vector<kabot::providers::ToolCallRequest>& tool_calls) const {
    kabot::providers::Message assistant_message{};
    assistant_message.role = "assistant";
    assistant_message.content = content;
    assistant_message.tool_calls = tool_calls;
    messages.push_back(assistant_message);
    return messages;
}

std::string ContextBuilder::LoadBootstrapFiles() const {
    static const std::vector<std::string> files = {
        "AGENTS.md", "SOUL.md", "USER.md", "TOOLS.md", "IDENTITY.md"
    };
    std::ostringstream oss;
    bool has_content = false;
    for (const auto& filename : files) {
        std::filesystem::path path = std::filesystem::path(workspace_) / filename;
        if (std::filesystem::exists(path)) {
            std::ifstream file(path);
            std::stringstream buffer;
            buffer << file.rdbuf();
            oss << "## " << filename << "\n\n" << buffer.str() << "\n\n";
            has_content = true;
        }
    }
    return has_content ? oss.str() : std::string();
}

std::string ContextBuilder::BuildQmdContext(const std::string& query) const {
    if (!qmd_.enabled || query.empty()) {
        return {};
    }

    std::string escaped;
    escaped.reserve(query.size());
    for (const char ch : query) {
        if (ch == '"') {
            escaped += "\\\"";
        } else if (ch == '\n' || ch == '\r') {
            escaped += ' ';
        } else {
            escaped += ch;
        }
    }

    std::ostringstream cmd;
    cmd << qmd_.command;
    if (!qmd_.index.empty()) {
        cmd << " --index " << qmd_.index;
    }
    cmd << " query --md";
    cmd << " --min-score " << qmd_.min_score;
    cmd << " -n " << qmd_.max_results;
    if (!qmd_.collection.empty()) {
        cmd << " -c " << qmd_.collection;
    }
    cmd << " \"" << escaped << "\"";

    const auto result = kabot::sandbox::SandboxExecutor::Run(
        cmd.str(),
        workspace_,
        std::chrono::seconds(qmd_.timeout_s));
    std::cerr << "[context] qmd_cmd=" << cmd.str() << std::endl;
    if (result.timed_out || result.exit_code != 0) {
        std::cerr << "[context] qmd_failed exit=" << result.exit_code
                  << " timeout=" << (result.timed_out ? "true" : "false")
                  << std::endl;
        if (!result.error.empty()) {
            std::cerr << "[context] qmd_error\n" << result.error << std::endl;
        }
        return {};
    }
    return result.output;
}

std::vector<kabot::providers::ContentPart> ContextBuilder::BuildUserContent(
    const std::string& text,
    const std::vector<std::string>& media) const {
    std::vector<kabot::providers::ContentPart> parts;
    if (media.empty()) {
        kabot::providers::ContentPart text_part{};
        text_part.type = "text";
        text_part.text = text;
        parts.push_back(text_part);
        return parts;
    }

    auto encode_base64 = [](const std::vector<unsigned char>& data) {
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        std::size_t i = 0;
        while (i < data.size()) {
            const std::size_t start = i;
            const unsigned int octet_a = i < data.size() ? data[i++] : 0;
            const unsigned int octet_b = i < data.size() ? data[i++] : 0;
            const unsigned int octet_c = i < data.size() ? data[i++] : 0;

            const unsigned int triple = (octet_a << 16) + (octet_b << 8) + octet_c;
            encoded.push_back(table[(triple >> 18) & 0x3F]);
            encoded.push_back(table[(triple >> 12) & 0x3F]);
            encoded.push_back(start + 1 < data.size() ? table[(triple >> 6) & 0x3F] : '=');
            encoded.push_back(start + 2 < data.size() ? table[triple & 0x3F] : '=');
        }
        return encoded;
    };

    auto mime_for = [](const std::filesystem::path& path) {
        const auto ext = path.extension().string();
        if (ext == ".png") return std::string("image/png");
        if (ext == ".jpg" || ext == ".jpeg") return std::string("image/jpeg");
        if (ext == ".gif") return std::string("image/gif");
        if (ext == ".webp") return std::string("image/webp");
        if (ext == ".bmp") return std::string("image/bmp");
        return std::string();
    };

    for (const auto& path_str : media) {
        std::filesystem::path path(path_str);
        const auto mime = mime_for(path);
        if (mime.empty() || !std::filesystem::exists(path)) {
            continue;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            continue;
        }
        std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        if (bytes.empty()) {
            continue;
        }
        const auto encoded = encode_base64(bytes);
        kabot::providers::ContentPart part{};
        part.type = "image_url";
        part.image_url = "data:" + mime + ";base64," + encoded;
        parts.push_back(part);
    }

    kabot::providers::ContentPart text_part{};
    text_part.type = "text";
    text_part.text = text;
    parts.push_back(text_part);

    return parts;
}

}  // namespace kabot::agent
