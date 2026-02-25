#include "agent/agent_loop.hpp"

#include <chrono>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "agent/tools/cron.hpp"
#include "agent/tools/filesystem.hpp"
#include "agent/tools/message.hpp"
#include "agent/tools/shell.hpp"
#include "agent/tools/spawn.hpp"
#include "agent/tools/tts.hpp"
#include "agent/tools/web.hpp"
#include "sandbox/sandbox_executor.hpp"

namespace kabot::agent {
namespace {

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string ExtractMemoryBlock(const std::string& content) {
    const std::string start_tag = "<kabot_memory>";
    const std::string end_tag = "</kabot_memory>";
    const auto start = content.find(start_tag);
    if (start == std::string::npos) {
        return {};
    }
    const auto end = content.find(end_tag, start + start_tag.size());
    if (end == std::string::npos) {
        return {};
    }
    const auto raw = content.substr(start + start_tag.size(), end - (start + start_tag.size()));
    return Trim(raw);
}

std::string StripMemoryBlock(const std::string& content) {
    const std::string start_tag = "<kabot_memory>";
    const std::string end_tag = "</kabot_memory>";
    const std::string open_prefix = "<kabot_memory";
    const std::string close_prefix = "</kabot_memory";
    std::string stripped = content;

    auto to_lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    };

    while (true) {
        const auto lower = to_lower(stripped);
        const auto start = lower.find(open_prefix);
        if (start == std::string::npos) {
            break;
        }
        auto tag_end = stripped.find('>', start + open_prefix.size());
        if (tag_end == std::string::npos) {
            stripped.erase(start, open_prefix.size());
            continue;
        }
        const auto end = lower.find(end_tag, tag_end + 1);
        if (end == std::string::npos) {
            stripped.erase(start, tag_end - start + 1);
            continue;
        }
        stripped.erase(start, end - start + end_tag.size());
    }

    while (true) {
        const auto lower = to_lower(stripped);
        const auto end_pos = lower.find(close_prefix);
        if (end_pos == std::string::npos) {
            break;
        }
        auto tag_end = stripped.find('>', end_pos + close_prefix.size());
        if (tag_end == std::string::npos) {
            stripped.erase(end_pos, close_prefix.size());
            continue;
        }
        stripped.erase(end_pos, tag_end - end_pos + 1);
    }

    while (true) {
        const auto lower = to_lower(stripped);
        const auto pos = lower.find(start_tag);
        if (pos == std::string::npos) {
            break;
        }
        stripped.erase(pos, start_tag.size());
    }
    while (true) {
        const auto lower = to_lower(stripped);
        const auto pos = lower.find(end_tag);
        if (pos == std::string::npos) {
            break;
        }
        stripped.erase(pos, end_tag.size());
    }

    return Trim(stripped);
}

std::vector<std::string> NormalizeMemoryLines(const std::string& block) {
    std::vector<std::string> lines;
    std::istringstream iss(block);
    std::string line;
    while (std::getline(iss, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("- ", 0) == 0) {
            line.erase(0, 2);
            line = Trim(line);
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

}  // namespace

AgentLoop::AgentLoop(
    kabot::bus::MessageBus& bus,
    kabot::providers::LLMProvider& provider,
    std::string workspace,
    kabot::config::AgentDefaults config,
    kabot::config::QmdConfig qmd,
    kabot::cron::CronService* cron)
    : bus_(bus)
    , provider_(provider)
    , workspace_(std::move(workspace))
    , config_(std::move(config))
    , qmd_(std::move(qmd))
    , context_(workspace_, qmd_)
    , sessions_(workspace_)
    , memory_(workspace_)
    , cron_(cron) {
    RegisterDefaultTools();
}

void AgentLoop::Run() {
    running_ = true;
    while (running_) {
        kabot::bus::InboundMessage msg{};
        if (!bus_.TryConsumeInbound(msg, std::chrono::milliseconds(1000))) {
            continue;
        }

        kabot::bus::OutboundMessage outbound{};
        try {
            if (msg.channel == "system") {
                outbound = ProcessSystemMessage(msg);
            } else {
                outbound = ProcessMessage(msg);
            }
        } catch (const std::exception& ex) {
            outbound.channel = msg.channel;
            outbound.chat_id = msg.chat_id;
            outbound.content = std::string("Sorry, I encountered an error: ") + ex.what();
        }

        bus_.PublishOutbound(outbound);
    }
}

void AgentLoop::Stop() {
    running_ = false;
}

std::string AgentLoop::ProcessDirect(const std::string& content, const std::string& session_key) {
    std::lock_guard<std::mutex> guard(process_mutex_);
    auto session = sessions_.GetOrCreate(session_key);
    auto history = session.GetHistory();
    auto messages = context_.BuildMessages(history, content, {});

    int iteration = 0;
    std::string final_content;
    bool message_sent = false;
    const auto model = config_.model.empty() ? provider_.GetDefaultModel() : config_.model;

    while (iteration < config_.max_iterations) {
        iteration += 1;
        auto response = provider_.Chat(
            messages,
            tools_.GetDefinitions(),
            model,
            config_.max_tokens,
            config_.temperature);

        if (response.HasToolCalls()) {
            messages = context_.AddAssistantMessage(messages, response.content, response.tool_calls);
            session.AddMessage("assistant", response.content, response.tool_calls);
            for (const auto& call : response.tool_calls) {
                if (call.name == "message") {
                    message_sent = true;
                }
                std::string result = tools_.Execute(call.name, call.arguments);
                messages = context_.AddToolResult(messages, call.id, call.name, result);
                session.AddToolMessage(call.id, call.name, result);
            }
        } else {
            final_content = response.content;
            break;
        }
    }

    if (final_content.empty()) {
        final_content = "Background task completed.";
    }

    const auto memory_block = ExtractMemoryBlock(final_content);
    final_content = StripMemoryBlock(final_content);
    session.AddMessage("user", content);
    session.AddMessage("assistant", final_content);
    sessions_.Save(session);
    AppendMemoryEntry(session_key, memory_block);
    return final_content;
}

kabot::bus::OutboundMessage AgentLoop::ProcessMessage(const kabot::bus::InboundMessage& msg) {
    std::lock_guard<std::mutex> guard(process_mutex_);
    const auto send_typing = [&]() {
        if (msg.channel != "telegram") {
            return;
        }
        kabot::bus::OutboundMessage typing{};
        typing.channel = msg.channel;
        typing.chat_id = msg.chat_id;
        typing.metadata["action"] = "typing";
        bus_.PublishOutbound(typing);
    };
    send_typing();
    if (auto* tool = tools_.Get("message")) {
        if (auto* message_tool = dynamic_cast<kabot::agent::tools::MessageTool*>(tool)) {
            message_tool->SetContext(msg.channel, msg.chat_id);
        }
    }
    std::string content = msg.content;
    bool reset_session = false;
    if (content.rfind("/new", 0) == 0) {
        reset_session = true;
        content = content.substr(4);
        const auto first_non_space = content.find_first_not_of(" \t\r\n");
        if (first_non_space == std::string::npos) {
            content.clear();
        } else if (first_non_space > 0) {
            content.erase(0, first_non_space);
        }
    }
    if (reset_session) {
        sessions_.Delete(msg.SessionKey());
        if (content.empty()) {
            kabot::bus::OutboundMessage outbound{};
            outbound.channel = msg.channel;
            outbound.chat_id = msg.chat_id;
            outbound.content = "已创建新会话，请发送新的问题。";
            return outbound;
        }
    }
    auto session = sessions_.GetOrCreate(msg.SessionKey());

    auto history = session.GetHistory();
    auto messages = context_.BuildMessages(
        history,
        content,
        msg.media);

    int iteration = 0;
    std::string final_content;
    bool message_sent = false;
    const auto model = config_.model.empty() ? provider_.GetDefaultModel() : config_.model;

    while (iteration < config_.max_iterations) {
        iteration += 1;
        auto response = provider_.Chat(
            messages,
            tools_.GetDefinitions(),
            model,
            config_.max_tokens,
            config_.temperature);

        if (response.HasToolCalls()) {
            messages = context_.AddAssistantMessage(messages, response.content, response.tool_calls);
            session.AddMessage("assistant", response.content, response.tool_calls);
            for (const auto& call : response.tool_calls) {
                if (call.name == "message") {
                    message_sent = true;
                }
                std::string result = tools_.Execute(call.name, call.arguments);
                messages = context_.AddToolResult(messages, call.id, call.name, result);
                session.AddToolMessage(call.id, call.name, result);
            }
        } else {
            final_content = response.content;
            break;
        }
    }

    if (final_content.empty()) {
        final_content = "I've completed processing but have no response to give.";
    }

    const auto memory_block = ExtractMemoryBlock(final_content);
    final_content = StripMemoryBlock(final_content);
    session.AddMessage("user", content);
    session.AddMessage("assistant", final_content);
    sessions_.Save(session);
    AppendMemoryEntry(msg.SessionKey(), memory_block);

    kabot::bus::OutboundMessage outbound{};
    if (!message_sent) {
        outbound.channel = msg.channel;
        outbound.chat_id = msg.chat_id;
        outbound.content = final_content;
    }
    return outbound;
}

void AgentLoop::RegisterDefaultTools() {
    tools_.Register(std::make_unique<kabot::agent::tools::ReadFileTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::WriteFileTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::EditFileTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::ListDirTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::ExecTool>(workspace_));
    if (!config_.brave_api_key.empty()) {
        const auto size = config_.brave_api_key.size();
        const auto prefix = size > 4 ? config_.brave_api_key.substr(0, 4) : config_.brave_api_key;
        std::cerr << "[web] brave api key=" << prefix << "***" << std::endl;
    } else {
        std::cerr << "[web] brave api key is empty" << std::endl;
    }
    tools_.Register(std::make_unique<kabot::agent::tools::WebSearchTool>(config_.brave_api_key));
    tools_.Register(std::make_unique<kabot::agent::tools::WebFetchTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::RedditFetchTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::MessageTool>(
        [this](const kabot::bus::OutboundMessage& msg) {
            bus_.PublishOutbound(msg);
        }));
    tools_.Register(std::make_unique<kabot::agent::tools::SpawnTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::EdgeTtsTool>(workspace_));
    if (cron_) {
        tools_.Register(std::make_unique<kabot::agent::tools::CronTool>(cron_));
    }
}

void AgentLoop::AppendMemoryEntry(const std::string& session_key,
                                  const std::string& memory_block) {
    if (memory_block.empty()) {
        return;
    }
    const auto lines = NormalizeMemoryLines(memory_block);
    if (lines.empty()) {
        return;
    }
    std::ostringstream entry;
    for (const auto& line : lines) {
        entry << "- [" << session_key << "] " << line << "\n";
    }
    memory_.AppendToday(entry.str());
}

void AgentLoop::UpdateQmdIndex() const {
    if (!qmd_.enabled || !qmd_.update_on_write) {
        return;
    }
    std::ostringstream cmd;
    cmd << qmd_.command;
    if (!qmd_.index.empty()) {
        cmd << " --index " << qmd_.index;
    }
    cmd << " update";
    if (qmd_.update_embeddings) {
        cmd << " && " << qmd_.command;
        if (!qmd_.index.empty()) {
            cmd << " --index " << qmd_.index;
        }
        cmd << " embed";
    }
    kabot::sandbox::SandboxExecutor::Run(
        cmd.str(),
        workspace_,
        std::chrono::seconds(qmd_.timeout_s));
}

kabot::bus::OutboundMessage AgentLoop::ProcessSystemMessage(const kabot::bus::InboundMessage& msg) {
    std::lock_guard<std::mutex> guard(process_mutex_);
    std::string origin_channel = "cli";
    std::string origin_chat_id = msg.chat_id;
    const auto delimiter = msg.chat_id.find(':');
    if (delimiter != std::string::npos) {
        origin_channel = msg.chat_id.substr(0, delimiter);
        origin_chat_id = msg.chat_id.substr(delimiter + 1);
    }

    if (auto* tool = tools_.Get("message")) {
        if (auto* message_tool = dynamic_cast<kabot::agent::tools::MessageTool*>(tool)) {
            message_tool->SetContext(origin_channel, origin_chat_id);
        }
    }

    const auto session_key = origin_channel + ":" + origin_chat_id;
    auto session = sessions_.GetOrCreate(session_key);
    auto messages = context_.BuildMessages(session.GetHistory(), msg.content, {});

    int iteration = 0;
    std::string final_content;
    bool message_sent = false;
    const auto model = config_.model.empty() ? provider_.GetDefaultModel() : config_.model;

    while (iteration < config_.max_iterations) {
        iteration += 1;
        auto response = provider_.Chat(
            messages,
            tools_.GetDefinitions(),
            model,
            config_.max_tokens,
            config_.temperature);

        if (response.HasToolCalls()) {
            messages = context_.AddAssistantMessage(messages, response.content, response.tool_calls);
            session.AddMessage("assistant", response.content, response.tool_calls);
            for (const auto& call : response.tool_calls) {
                if (call.name == "message") {
                    message_sent = true;
                }
                std::string result = tools_.Execute(call.name, call.arguments);
                messages = context_.AddToolResult(messages, call.id, call.name, result);
                session.AddToolMessage(call.id, call.name, result);
            }
        } else {
            final_content = response.content;
            break;
        }
    }

    if (final_content.empty()) {
        final_content = "Background task completed.";
    }

    const auto memory_block = ExtractMemoryBlock(final_content);
    final_content = StripMemoryBlock(final_content);
    session.AddMessage("user", "[System] " + msg.content);
    session.AddMessage("assistant", final_content);
    sessions_.Save(session);
    AppendMemoryEntry(session_key, memory_block);

    kabot::bus::OutboundMessage outbound{};
    if (!message_sent) {
        outbound.channel = origin_channel;
        outbound.chat_id = origin_chat_id;
        outbound.content = final_content;
    }
    return outbound;
}

}  // namespace kabot::agent
