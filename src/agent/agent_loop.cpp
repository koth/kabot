#include "agent/agent_loop.hpp"

#include <chrono>
#include <sstream>
#include <iostream>
#include <utility>
#include <vector>

#include "agent/tools/filesystem.hpp"
#include "agent/tools/message.hpp"
#include "agent/tools/shell.hpp"
#include "agent/tools/spawn.hpp"
#include "agent/tools/web.hpp"
#include "sandbox/sandbox_executor.hpp"

namespace kabot::agent {

AgentLoop::AgentLoop(
    kabot::bus::MessageBus& bus,
    kabot::providers::LLMProvider& provider,
    std::string workspace,
    kabot::config::AgentDefaults config,
    kabot::config::QmdConfig qmd)
    : bus_(bus)
    , provider_(provider)
    , workspace_(std::move(workspace))
    , config_(std::move(config))
    , qmd_(std::move(qmd))
    , context_(workspace_, qmd_)
    , sessions_(workspace_)
    , memory_(workspace_) {
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

    session.AddMessage("user", content);
    session.AddMessage("assistant", final_content);
    sessions_.Save(session);
    AppendMemoryEntry(session_key, content, final_content);
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

    session.AddMessage("user", content);
    session.AddMessage("assistant", final_content);
    sessions_.Save(session);
    AppendMemoryEntry(msg.SessionKey(), content, final_content);

    kabot::bus::OutboundMessage outbound{};
    outbound.channel = msg.channel;
    outbound.chat_id = msg.chat_id;
    outbound.content = final_content;
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
}

void AgentLoop::AppendMemoryEntry(const std::string& session_key,
                                  const std::string& user_content,
                                  const std::string& assistant_content) {
    std::ostringstream entry;
    entry << "- [" << session_key << "] user: " << user_content << "\n";
    entry << "  assistant: " << assistant_content << "\n";
    memory_.AppendToday(entry.str());
    UpdateQmdIndex();
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

    session.AddMessage("user", "[System] " + msg.content);
    session.AddMessage("assistant", final_content);
    sessions_.Save(session);
    AppendMemoryEntry(session_key, msg.content, final_content);

    kabot::bus::OutboundMessage outbound{};
    outbound.channel = origin_channel;
    outbound.chat_id = origin_chat_id;
    outbound.content = final_content;
    return outbound;
}

}  // namespace kabot::agent
