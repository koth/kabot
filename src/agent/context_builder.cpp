#include "agent/context_builder.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>

#include "sandbox/sandbox_executor.hpp"
#include "utils/logging.hpp"

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
#if defined(_WIN32)
    oss << "## Execution Environment\n";
    oss << "Host OS: Windows\n";
    oss << "Command executor: cmd.exe /C\n";
    oss << "When generating commands for shell-like tools, prefer Windows cmd-compatible syntax.\n";
    oss << "Do not assume bash, zsh, or other Unix shell syntax.\n";
    oss << "Do not use PowerShell-only syntax unless you explicitly invoke powershell -Command.\n\n";
#else
    oss << "## Execution Environment\n";
    oss << "Host OS: Unix-like\n";
    oss << "Command executor: /bin/sh -c\n";
    oss << "When generating commands for shell-like tools, prefer POSIX shell syntax.\n";
    oss << "Do not assume PowerShell syntax.\n\n";
#endif

    const auto bootstrap = LoadBootstrapFiles();
    if (!bootstrap.empty()) {
        oss << bootstrap << "\n\n";
    }

    

    oss << "## Tool Use\n";
    oss << "When the user asks you to inspect files, read code, modify code, write files, run commands, browse the web, fetch external data, send messages, or schedule work, do not claim the task is completed unless you have already called the relevant tool and received its result.\n";
    oss << "If tools are required but unavailable or insufficient, explicitly say what is missing instead of pretending success.\n";
    oss << "If you are giving reasoning or advice without executing anything, make that distinction explicit.\n\n";

    oss << "## Memory Writing\n";
    oss << "Only record durable information: preferences, long-term facts, goal changes, key conclusions, task summaries.\n";
    oss << "If such memory appears in this turn (or the user explicitly marks it as important), append a block at the end of your reply in the format:\n\n";
    oss << "<kabot_memory>\n- item\n</kabot_memory>\n\n";
    oss << "If there is nothing to store, omit the block.\n\n";

    oss << "## Daily Memory\n";
    oss << "Daily memory is stored at: " << workspace_ << "/memory/YYYY-MM-DD.md\n\n";


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

    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm{};
#if defined(_WIN32)
    localtime_s(&now_tm, &now_time);
#else
    localtime_r(&now_time, &now_tm);
#endif
    oss << "Current time: " << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S %Z") << "\n\n";


    std::string memory;
    if (qmd_.enabled) {
        LOG_DEBUG("[context] memory=QMD");
        if (!current_message.empty()) {
            LOG_DEBUG("[context] qmd_query={}", current_message);
            memory = BuildQmdContext(current_message);
            if (!memory.empty() && false) {
                LOG_DEBUG("[context] qmd_memory\n{}", memory);
            }
        }
    } else {
        LOG_DEBUG("[context] memory=FULL");
        memory = memory_.GetMemoryContext();
    }
    if (!memory.empty()) {
        oss << "# Memory\n\n" << memory << "\n\n";
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
    const std::vector<kabot::providers::ToolCallRequest>& tool_calls,
    const std::unordered_map<std::string, int>& usage) const {
    kabot::providers::Message assistant_message{};
    assistant_message.role = "assistant";
    assistant_message.content = content;
    assistant_message.tool_calls = tool_calls;
    assistant_message.usage = usage;
    messages.push_back(assistant_message);
    return messages;
}

namespace {

std::size_t RoughEstimateMessageTokens(const kabot::providers::Message& msg) {
    std::size_t chars = 0;
    if (!msg.content.empty()) {
        chars += msg.content.size();
    }
    for (const auto& part : msg.content_parts) {
        if (!part.text.empty()) {
            chars += part.text.size();
        }
        if (!part.image_url.empty()) {
            chars += 256; // rough estimate for image reference
        }
    }
    for (const auto& call : msg.tool_calls) {
        chars += call.name.size();
        for (const auto& [k, v] : call.arguments) {
            chars += k.size() + v.size();
        }
    }
    return chars / 4 + 1;
}

} // namespace

namespace {

constexpr std::size_t kToolResultMaxChars = 12000;

std::string TruncateContent(const std::string& value, std::size_t max_len) {
    if (value.size() <= max_len) {
        return value;
    }
    return value.substr(0, max_len) +
           "\n...[truncated, original size=" +
           std::to_string(value.size()) +
           " chars]...";
}

} // namespace

std::vector<kabot::providers::Message> ContextBuilder::ProjectMessages(
    std::vector<kabot::providers::Message> messages) const {
    // 1. Remove virtual messages
    messages.erase(
        std::remove_if(messages.begin(), messages.end(),
            [](const auto& m) { return m.is_virtual; }),
        messages.end());

    // 2. Determine protected tail: keep the most recent 2 complete turns + current input.
    //    A "turn" starts at a user message. We walk backwards and count user messages.
    std::size_t protected_start = 0;
    int users_seen = 0;
    for (std::size_t i = messages.size(); i-- > 0;) {
        if (messages[i].role == "user") {
            users_seen++;
            if (users_seen >= 3) {
                protected_start = i;
                break;
            }
        }
    }
    // For short conversations (fewer than 3 users), fall back to protecting the most
    // recent assistant and everything after it, so we still truncate oversized old content.
    if (users_seen < 3) {
        protected_start = messages.size();
        for (std::size_t i = messages.size(); i-- > 0;) {
            if (messages[i].role == "assistant") {
                protected_start = i;
                break;
            }
        }
    }

    // 3. Strip oversized tool results and multimedia placeholders outside the protected tail.
    for (std::size_t idx = 0; idx < messages.size() && idx < protected_start; ++idx) {
        auto& msg = messages[idx];
        if (msg.role == "tool" && msg.content.size() > kToolResultMaxChars) {
            msg.content = TruncateContent(msg.content, kToolResultMaxChars);
        }
        if (!msg.content_parts.empty()) {
            bool has_media = false;
            for (const auto& part : msg.content_parts) {
                if (part.type == "image_url") {
                    has_media = true;
                    break;
                }
            }
            if (has_media) {
                std::vector<kabot::providers::ContentPart> stripped;
                for (auto& part : msg.content_parts) {
                    if (part.type == "image_url") {
                        kabot::providers::ContentPart placeholder{};
                        placeholder.type = "text";
                        placeholder.text = "[image]";
                        stripped.push_back(std::move(placeholder));
                    } else {
                        stripped.push_back(std::move(part));
                    }
                }
                msg.content_parts = std::move(stripped);
            }
        }
    }

    // 4. Snip: physically drop old messages outside the protected tail,
    //    but keep system messages at the head.
    std::vector<kabot::providers::Message> snipped;
    snipped.reserve(messages.size());
    for (std::size_t idx = 0; idx < messages.size(); ++idx) {
        if (messages[idx].role == "system" || idx >= protected_start) {
            snipped.push_back(std::move(messages[idx]));
        }
    }
    messages = std::move(snipped);

    // 5. Force pair tool_use/tool_result blocks.
    std::unordered_set<std::string> pending_tool_use_ids;
    std::unordered_set<std::string> seen_tool_result_ids;
    for (const auto& msg : messages) {
        if (msg.role == "assistant") {
            for (const auto& call : msg.tool_calls) {
                pending_tool_use_ids.insert(call.id);
            }
        }
        if (msg.role == "tool" && !msg.tool_call_id.empty()) {
            seen_tool_result_ids.insert(msg.tool_call_id);
        }
    }
    for (const auto& id : pending_tool_use_ids) {
        if (seen_tool_result_ids.find(id) == seen_tool_result_ids.end()) {
            kabot::providers::Message placeholder{};
            placeholder.role = "tool";
            placeholder.tool_call_id = id;
            placeholder.name = "unknown";
            placeholder.content = "[Tool result missing due to internal error]";
            messages.push_back(std::move(placeholder));
        }
    }

    // 6. Coalesce stray system-reminder text blocks into adjacent tool messages
    std::vector<kabot::providers::Message> merged;
    merged.reserve(messages.size());
    for (auto& msg : messages) {
        if (!merged.empty() && msg.role == "user" &&
            !msg.content.empty() && msg.content.find("<system-reminder>") == 0 &&
            merged.back().role == "tool") {
            merged.back().content = msg.content + "\n" + merged.back().content;
            continue;
        }
        merged.push_back(std::move(msg));
    }

    return merged;
}

std::size_t ContextBuilder::EstimateTokens(
    const std::vector<kabot::providers::Message>& messages) const {
    // Look backwards for the last assistant message with a recorded usage baseline.
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "assistant") {
            auto usage_it = it->usage.find("total_tokens");
            if (usage_it != it->usage.end() && usage_it->second > 0) {
                std::size_t baseline = static_cast<std::size_t>(usage_it->second);
                std::size_t anchor_index = static_cast<std::size_t>(
                    std::distance(it, messages.rend()) - 1);
                std::size_t added_est = 0;
                for (std::size_t i = anchor_index + 1; i < messages.size(); ++i) {
                    added_est += RoughEstimateMessageTokens(messages[i]);
                }
                return baseline + added_est;
            }
        }
    }
    // No baseline found: rough estimate everything.
    std::size_t total = 0;
    for (const auto& msg : messages) {
        total += RoughEstimateMessageTokens(msg);
    }
    return total;
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
    LOG_DEBUG("[context] qmd_cmd={}", cmd.str());
    if (result.timed_out || result.exit_code != 0) {
        LOG_WARN("[context] qmd_failed exit={} timeout={}",
                 result.exit_code,
                 (result.timed_out ? "true" : "false"));
        if (!result.error.empty()) {
            LOG_WARN("[context] qmd_error\n{}", result.error);
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
