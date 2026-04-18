#include "agent/agent_loop.hpp"

#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>
#include <vector>

#include "agent/tools/cron.hpp"
#include "agent/tools/filesystem.hpp"
#include "agent/tools/message.hpp"
#include "agent/tools/plan_work.hpp"
#include "agent/tools/shell.hpp"
#include "agent/tools/spawn.hpp"
#include "agent/tools/todo.hpp"
#include "agent/tools/tts.hpp"
#include "agent/tools/web.hpp"
#include "sandbox/sandbox_executor.hpp"
#include "utils/logging.hpp"

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

std::string PhaseSummary(DirectExecutionPhase phase) {
    switch (phase) {
    case DirectExecutionPhase::kReceived:
        return "Received message";
    case DirectExecutionPhase::kProcessing:
        return "Processing message";
    case DirectExecutionPhase::kCallingTools:
        return "Calling tools";
    case DirectExecutionPhase::kReplying:
        return "Preparing reply";
    }
    return "Processing message";
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

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool ContainsAnyKeyword(const std::string& haystack,
                        const std::initializer_list<const char*> keywords) {
    for (const auto* keyword : keywords) {
        if (haystack.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool RequiresToolGuardrail(const std::string& content) {
    const auto lower = ToLower(content);
    return ContainsAnyKeyword(lower,
                              {"read ",
                               "open ",
                               "check ",
                               "look at",
                               "inspect",
                               "search",
                               "fetch",
                               "browse",
                               "web",
                               "http",
                               "curl",
                               "file",
                               "folder",
                               "directory",
                               "code",
                               "edit",
                               "modify",
                               "change",
                               "write ",
                               "create ",
                               "run ",
                               "execute",
                               "command",
                               "shell",
                               "terminal",
                               "send",
                               "message",
                               "cron",
                               "schedule",
                               "定时",
                               "读取",
                               "查看",
                               "检查",
                               "搜索",
                               "查下",
                               "查一",
                               "修改",
                               "改下",
                               "编辑",
                               "写入",
                               "创建",
                               "生成文件",
                               "运行",
                               "执行",
                               "命令",
                               "终端",
                               "发消息",
                               "发送",
                               "抓取",
                               "网页",
                               "联网"});
}

std::string BuildToolGuardrailMessage(const std::string& original_request) {
    std::ostringstream oss;
    oss << "你刚才那条用户请求很可能需要外部观察或实际副作用。";
    oss << "在你真正调用所需工具并拿到结果之前，不要声称任务已经完成。";
    oss << "如果需要工具，请现在立即调用；如果现有工具不足，请明确说明缺少什么，不要假装已经成功完成。";
    if (!original_request.empty()) {
        oss << "原始请求：" << original_request;
    }
    return oss.str();
}

void AppendGuardrailUserMessage(std::vector<kabot::providers::Message>& messages,
                                const std::string& original_request) {
    kabot::providers::Message reminder{};
    reminder.role = "user";
    reminder.content = BuildToolGuardrailMessage(original_request);
    messages.push_back(std::move(reminder));
}

void AppendPostToolReminder(std::vector<kabot::providers::Message>& messages) {
    kabot::providers::Message reminder{};
    reminder.role = "user";
    reminder.content = "你刚刚执行了工具调用并收到了结果。请基于这些结果给出一个清晰、完整的回复，不要留空。";
    messages.push_back(std::move(reminder));
}

}  // namespace

std::string DirectExecutionPhaseSummary(DirectExecutionPhase phase) {
    return PhaseSummary(phase);
}

AgentLoop::AgentLoop(
    kabot::bus::MessageBus& bus,
    kabot::providers::LLMProvider& provider,
    std::string workspace,
    kabot::config::AgentDefaults config,
    kabot::config::QmdConfig qmd,
    kabot::config::TaskSystemConfig task_system,
    kabot::cron::CronService* cron)
    : bus_(bus)
    , provider_(provider)
    , workspace_(std::move(workspace))
    , config_(std::move(config))
    , qmd_(std::move(qmd))
    , task_system_(std::move(task_system))
    , context_(workspace_, qmd_)
    , sessions_(workspace_)
    , memory_(workspace_)
    , cron_(cron) {
    subagent_service_ = std::make_unique<kabot::subagent::SubagentService>(
        provider_, tools_, workspace_, static_cast<kabot::config::AgentDefaults>(config_));
    subagent_service_->SetTaskCompletionHandler([this](const kabot::subagent::AgentTaskRecord& task) {
        if (task.parent_session_id.empty()) return;
        auto session_opt = sessions_.Get(task.parent_session_id);
        if (!session_opt) return;
        auto session = *session_opt;
        std::string status_str = "completed";
        if (task.status == kabot::subagent::SubagentStatus::kFailed) {
            status_str = "failed";
        } else if (task.status == kabot::subagent::SubagentStatus::kAborted) {
            status_str = "killed";
        }
        std::ostringstream oss;
        oss << "<task-notification agent_id=\"" << task.agent_id << "\" status=\"" << status_str << "\">\n";
        oss << "Task: " << task.description << "\n";
        if (task.status == kabot::subagent::SubagentStatus::kCompleted) {
            oss << "Summary: " << task.output_file << "\n";
            oss << "total_tokens: " << task.total_tokens << "\n";
        } else {
            oss << "Error: " << task.error.code << " - " << task.error.message << "\n";
        }
        oss << "</task-notification>";
        session.AddPendingNotification(oss.str());
        sessions_.Save(session);
    });
    RegisterDefaultTools();
}

void AgentLoop::SetRelayManager(kabot::relay::RelayManager* relay_manager) {
    relay_manager_ = relay_manager;
    if (auto* tool = tools_.Get("plan_work")) {
        if (auto* plan_tool = dynamic_cast<kabot::agent::tools::PlanWorkTool*>(tool)) {
            plan_tool->SetRelayManager(relay_manager);
            plan_tool->SetContext({}, {}, {}, {});
        }
    }
}

void AgentLoop::Run() {
    running_ = true;
    while (running_) {
        kabot::bus::InboundMessage msg{};
        if (!bus_.TryConsumeInbound(msg, std::chrono::milliseconds(1000))) {
            continue;
        }

        auto outbound = HandleInbound(msg);
        bus_.PublishOutbound(outbound);
    }
}

void AgentLoop::Stop() {
    running_ = false;
}

kabot::bus::OutboundMessage AgentLoop::HandleInbound(
    const kabot::bus::InboundMessage& msg,
    const DirectExecutionObserver& observer,
    const std::function<void(bool, const std::string&)>& completion) {
    kabot::bus::OutboundMessage outbound{};
    try {
        if (msg.channel == "system") {
            outbound = ProcessSystemMessage(msg);
        } else {
            outbound = ProcessMessage(msg, observer);
        }
        if (completion) {
            completion(true, outbound.content);
        }
    } catch (const std::exception& ex) {
        outbound.channel = msg.channel;
        outbound.channel_instance = msg.channel_instance;
        outbound.agent_name = msg.agent_name;
        outbound.chat_id = msg.chat_id;
        outbound.content = std::string("Error: ") + ex.what();
        if (completion) {
            completion(false, ex.what());
        }
    } catch (...) {
        outbound.channel = msg.channel;
        outbound.channel_instance = msg.channel_instance;
        outbound.agent_name = msg.agent_name;
        outbound.chat_id = msg.chat_id;
        outbound.content = "Error: unknown exception";
        if (completion) {
            completion(false, "unknown exception");
        }
    }
    return outbound;
}

std::vector<std::string> AgentLoop::RegisteredTools() const {
    return tools_.List();
}

kabot::session::Session AgentLoop::GetSession(const std::string& session_key) {
    auto session = sessions_.GetOrCreate(session_key);
    return session;
}

std::string AgentLoop::ProcessDirect(const std::string& content,
                                     const std::string& session_key,
                                     const DirectExecutionObserver& observer,
                                     const DirectExecutionTarget& target,
                                     const DirectOutboundObserver& outbound_observer,
                                     const kabot::CancelToken& cancel_token) {
    std::lock_guard<std::mutex> guard(process_mutex_);
    if (auto* tool = tools_.Get("send_message")) {
        if (auto* message_tool = dynamic_cast<kabot::agent::tools::SendMessageTool*>(tool)) {
            const auto target_channel = target.channel_instance.empty() ? target.channel : target.channel_instance;
            message_tool->SetContext(target_channel, target.chat_id);
            message_tool->SetObserver(outbound_observer);
        }
    }
    if (auto* tool = tools_.Get("plan_work")) {
        if (auto* plan_tool = dynamic_cast<kabot::agent::tools::PlanWorkTool*>(tool)) {
            const auto target_channel = target.channel_instance.empty() ? target.channel : target.channel_instance;
            plan_tool->SetContext(target.channel, target.channel_instance, target.chat_id, {});
        }
    }
    if (auto* tool = tools_.Get("cron")) {
        if (auto* cron_tool = dynamic_cast<kabot::agent::tools::CronTool*>(tool)) {
            const auto target_channel = target.channel_instance.empty() ? target.channel : target.channel_instance;
            cron_tool->SetContext({}, target_channel, target.chat_id);
        }
    }
    auto session = sessions_.GetOrCreate(session_key);
    auto history = session.GetHistory(static_cast<std::size_t>(config_.max_history_messages));
    auto messages = context_.BuildMessages(history, content, {});
    for (const auto& notif : session.TakePendingNotifications()) {
        kabot::providers::Message notif_msg;
        notif_msg.role = "user";
        notif_msg.content = "[System notification] " + notif + "\n\n(This is an automated notification, not a user message. Do not respond to it directly.)";
        notif_msg.is_virtual = true;
        messages.push_back(std::move(notif_msg));
    }
    DirectExecutionPhase last_phase = DirectExecutionPhase::kReceived;
    const auto notify_phase = [&](DirectExecutionPhase phase) {
        if (!observer) {
            return;
        }
        if (phase == last_phase) {
            return;
        }
        observer(phase);
        last_phase = phase;
    };

    int iteration = 0;
    std::string final_content;
    bool message_sent = false;
    const auto model = config_.model.empty() ? provider_.GetDefaultModel() : config_.model;
    const bool requires_tool_guardrail = RequiresToolGuardrail(content);
    bool tool_called = false;
    bool guardrail_retry_used = false;
    bool post_tool_reminder_used = false;

    LOG_INFO("[agent] process_direct tool_guardrail={} session={}",
             (requires_tool_guardrail ? "true" : "false"),
             session_key);
    if (observer) {
        observer(DirectExecutionPhase::kReceived);
    }
    notify_phase(DirectExecutionPhase::kProcessing);

    while (iteration < config_.max_iterations) {
        if (cancel_token.IsCancelled()) {
            throw std::runtime_error("Task cancelled by timeout or stop request");
        }
        iteration += 1;

        auto projected = context_.ProjectMessages(messages);
        const auto estimated_tokens = context_.EstimateTokens(projected);
        LOG_DEBUG("[agent] estimated_tokens={} session={}", estimated_tokens, session_key);

        auto response = provider_.Chat(
            projected,
            tools_.GetDefinitions(),
            model,
            config_.max_tokens,
            config_.temperature);

        if (response.HasToolCalls()) {
            tool_called = true;
            notify_phase(DirectExecutionPhase::kCallingTools);
            messages = context_.AddAssistantMessage(messages, response.content, response.tool_calls, response.usage);
            session.AddMessage("assistant", response.content, response.tool_calls, response.usage);
            bool plan_work_called = false;
            for (const auto& call : response.tool_calls) {
                if (cancel_token.IsCancelled()) {
                    throw std::runtime_error("Task cancelled by timeout or stop request");
                }
                if (call.name == "send_message") {
                    message_sent = true;
                }
                std::string result = ExecuteToolWithGuardrails(session, call);
                if (call.name == "plan_work") {
                    plan_work_called = true;
                    final_content = result;
                }
                messages = context_.AddToolResult(messages, call.id, call.name, result);
                session.AddToolMessage(call.id, call.name, result);
            }
            notify_phase(DirectExecutionPhase::kProcessing);
            if (plan_work_called) {
                break;
            }
        } else {
            if (requires_tool_guardrail && !tool_called && !guardrail_retry_used) {
                guardrail_retry_used = true;
                LOG_WARN("[agent] process_direct blocked non-tool completion finish_reason={} session={}",
                         response.finish_reason,
                         session_key);
                AppendGuardrailUserMessage(messages, content);
                continue;
            }
            if (tool_called && Trim(response.content).empty() && !post_tool_reminder_used) {
                post_tool_reminder_used = true;
                LOG_WARN("[agent] process_direct empty post-tool response, appending reminder session={}", session_key);
                AppendPostToolReminder(messages);
                continue;
            }
            notify_phase(DirectExecutionPhase::kReplying);
            final_content = response.content;
            break;
        }
    }

    LOG_INFO("[agent] process_direct completed tool_called={} guardrail_retry_used={} message_sent={} session={}",
             (tool_called ? "true" : "false"),
             (guardrail_retry_used ? "true" : "false"),
             (message_sent ? "true" : "false"),
             session_key);

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

kabot::bus::OutboundMessage AgentLoop::ProcessMessage(const kabot::bus::InboundMessage& msg,
                                                       const DirectExecutionObserver& observer) {
    std::lock_guard<std::mutex> guard(process_mutex_);
    const auto send_typing = [&]() {
        if (msg.channel != "telegram") {
            return;
        }
        kabot::bus::OutboundMessage typing{};
        typing.channel = msg.channel;
        typing.channel_instance = msg.channel_instance;
        typing.agent_name = msg.agent_name;
        typing.chat_id = msg.chat_id;
        typing.metadata["action"] = "typing";
        bus_.PublishOutbound(typing);
    };
    send_typing();
    if (auto* tool = tools_.Get("send_message")) {
        if (auto* message_tool = dynamic_cast<kabot::agent::tools::SendMessageTool*>(tool)) {
            message_tool->SetContext(msg.channel, msg.chat_id);
            message_tool->SetObserver({});
        }
    }
    if (auto* tool = tools_.Get("cron")) {
        if (auto* cron_tool = dynamic_cast<kabot::agent::tools::CronTool*>(tool)) {
            cron_tool->SetContext(msg.agent_name, msg.channel_instance.empty() ? msg.channel : msg.channel_instance, msg.chat_id);
        }
    }
    if (auto* tool = tools_.Get("plan_work")) {
        if (auto* plan_tool = dynamic_cast<kabot::agent::tools::PlanWorkTool*>(tool)) {
            plan_tool->SetContext(msg.channel, msg.channel_instance, msg.chat_id, msg.agent_name);
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
            outbound.channel_instance = msg.channel_instance;
            outbound.agent_name = msg.agent_name;
            outbound.chat_id = msg.chat_id;
            outbound.content = "已创建新会话，请发送新的问题。";
            return outbound;
        }
    }
    auto session = sessions_.GetOrCreate(msg.SessionKey());

    auto history = session.GetHistory(static_cast<std::size_t>(config_.max_history_messages));
    auto messages = context_.BuildMessages(
        history,
        content,
        msg.media);
    for (const auto& notif : session.TakePendingNotifications()) {
        kabot::providers::Message notif_msg;
        notif_msg.role = "user";
        notif_msg.content = "[System notification] " + notif + "\n\n(This is an automated notification, not a user message. Do not respond to it directly.)";
        notif_msg.is_virtual = true;
        messages.push_back(std::move(notif_msg));
    }

    int iteration = 0;
    std::string final_content;
    bool message_sent = false;
    const auto model = config_.model.empty() ? provider_.GetDefaultModel() : config_.model;
    const bool requires_tool_guardrail = RequiresToolGuardrail(content);
    bool tool_called = false;
    bool guardrail_retry_used = false;
    bool post_tool_reminder_used = false;

    LOG_INFO("[agent] process_message tool_guardrail={} session={}",
             (requires_tool_guardrail ? "true" : "false"),
             msg.SessionKey());
    DirectExecutionPhase last_phase = DirectExecutionPhase::kReceived;
    const auto notify_phase = [&](DirectExecutionPhase phase) {
        if (!observer) {
            return;
        }
        if (phase == last_phase) {
            return;
        }
        observer(phase);
        last_phase = phase;
    };
    if (observer) {
        observer(DirectExecutionPhase::kReceived);
    }
    notify_phase(DirectExecutionPhase::kProcessing);

    while (iteration < config_.max_iterations) {
        iteration += 1;

        auto projected = context_.ProjectMessages(messages);
        const auto estimated_tokens = context_.EstimateTokens(projected);
        LOG_DEBUG("[agent] estimated_tokens={} session={}", estimated_tokens, msg.SessionKey());

        auto response = provider_.Chat(
            projected,
            tools_.GetDefinitions(),
            model,
            config_.max_tokens,
            config_.temperature);

        if (response.HasToolCalls()) {
            tool_called = true;
            notify_phase(DirectExecutionPhase::kCallingTools);
            messages = context_.AddAssistantMessage(messages, response.content, response.tool_calls, response.usage);
            session.AddMessage("assistant", response.content, response.tool_calls, response.usage);
            bool plan_work_called = false;
            for (const auto& call : response.tool_calls) {
                if (call.name == "send_message") {
                    message_sent = true;
                }
                std::string result = ExecuteToolWithGuardrails(session, call);
                if (call.name == "plan_work") {
                    plan_work_called = true;
                    final_content = result;
                }
                messages = context_.AddToolResult(messages, call.id, call.name, result);
                session.AddToolMessage(call.id, call.name, result);
            }
            notify_phase(DirectExecutionPhase::kProcessing);
            if (plan_work_called) {
                break;
            }
        } else {
            if (requires_tool_guardrail && !tool_called && !guardrail_retry_used) {
                guardrail_retry_used = true;
                LOG_WARN("[agent] process_message blocked non-tool completion finish_reason={} session={}",
                         response.finish_reason,
                         msg.SessionKey());
                AppendGuardrailUserMessage(messages, content);
                continue;
            }
            if (tool_called && Trim(response.content).empty() && !post_tool_reminder_used) {
                post_tool_reminder_used = true;
                LOG_WARN("[agent] process_message empty post-tool response, appending reminder session={}", msg.SessionKey());
                AppendPostToolReminder(messages);
                continue;
            }
            notify_phase(DirectExecutionPhase::kReplying);
            final_content = response.content;
            break;
        }
    }

    LOG_INFO("[agent] process_message completed tool_called={} guardrail_retry_used={} message_sent={} session={}",
             (tool_called ? "true" : "false"),
             (guardrail_retry_used ? "true" : "false"),
             (message_sent ? "true" : "false"),
             msg.SessionKey());

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
        outbound.channel_instance = msg.channel_instance;
        outbound.agent_name = msg.agent_name;
        outbound.chat_id = msg.chat_id;
        outbound.content = final_content;
    }
    return outbound;
}

void AgentLoop::RegisterDefaultTools() {
    tools_.Register(std::make_unique<kabot::agent::tools::SendMessageTool>(
        [this](const kabot::bus::OutboundMessage& msg) {
            bus_.PublishOutbound(msg);
        }));

    if (config_.tool_profile == "message_only") {
        return;
    }

    tools_.Register(std::make_unique<kabot::agent::tools::ReadFileTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::WriteFileTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::EditFileTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::ListDirTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::GlobTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::GrepTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::BashTool>(workspace_));
    if (!config_.brave_api_key.empty()) {
        const auto size = config_.brave_api_key.size();
        const auto prefix = size > 4 ? config_.brave_api_key.substr(0, 4) : config_.brave_api_key;
        LOG_INFO("[web] brave api key={}***", prefix);
    } else {
        LOG_WARN("[web] brave api key is empty");
    }
    tools_.Register(std::make_unique<kabot::agent::tools::WebSearchTool>(config_.brave_api_key));
    tools_.Register(std::make_unique<kabot::agent::tools::WebFetchTool>(workspace_));
    tools_.Register(std::make_unique<kabot::agent::tools::RedditFetchTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::AgentTool>(
        [this](const kabot::subagent::AgentSpawnInput& input) {
            return this->SpawnSubagent(input);
        }));
    tools_.Register(std::make_unique<kabot::agent::tools::TodoWriteTool>());
    tools_.Register(std::make_unique<kabot::agent::tools::EdgeTtsTool>(workspace_));
    if (cron_) {
        tools_.Register(std::make_unique<kabot::agent::tools::CronTool>(cron_));
    }
    tools_.Register(std::make_unique<kabot::agent::tools::PlanWorkTool>(
        provider_, task_system_, relay_manager_));
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

    if (auto* tool = tools_.Get("send_message")) {
        if (auto* message_tool = dynamic_cast<kabot::agent::tools::SendMessageTool*>(tool)) {
            message_tool->SetContext(origin_channel, origin_chat_id);
            message_tool->SetObserver({});
        }
    }
    if (auto* tool = tools_.Get("cron")) {
        if (auto* cron_tool = dynamic_cast<kabot::agent::tools::CronTool*>(tool)) {
            cron_tool->SetContext(msg.agent_name, origin_channel, origin_chat_id);
        }
    }

    const auto session_key = origin_channel + ":" + origin_chat_id;
    auto session = sessions_.GetOrCreate(session_key);
    auto messages = context_.BuildMessages(session.GetHistory(), msg.content, {});
    for (const auto& notif : session.TakePendingNotifications()) {
        kabot::providers::Message notif_msg;
        notif_msg.role = "user";
        notif_msg.content = "[System notification] " + notif + "\n\n(This is an automated notification, not a user message. Do not respond to it directly.)";
        notif_msg.is_virtual = true;
        messages.push_back(std::move(notif_msg));
    }

    int iteration = 0;
    std::string final_content;
    bool message_sent = false;
    const auto model = config_.model.empty() ? provider_.GetDefaultModel() : config_.model;
    const bool requires_tool_guardrail = RequiresToolGuardrail(msg.content);
    bool tool_called = false;
    bool guardrail_retry_used = false;
    bool post_tool_reminder_used = false;

    LOG_INFO("[agent] process_system_message tool_guardrail={} session={}",
             (requires_tool_guardrail ? "true" : "false"),
             session_key);

    while (iteration < config_.max_iterations) {
        iteration += 1;

        auto projected = context_.ProjectMessages(messages);
        const auto estimated_tokens = context_.EstimateTokens(projected);
        LOG_DEBUG("[agent] estimated_tokens={} session={}", estimated_tokens, session_key);

        auto response = provider_.Chat(
            projected,
            tools_.GetDefinitions(),
            model,
            config_.max_tokens,
            config_.temperature);

        if (response.HasToolCalls()) {
            tool_called = true;
            messages = context_.AddAssistantMessage(messages, response.content, response.tool_calls, response.usage);
            session.AddMessage("assistant", response.content, response.tool_calls, response.usage);
            bool plan_work_called = false;
            for (const auto& call : response.tool_calls) {
                if (call.name == "send_message") {
                    message_sent = true;
                }
                std::string result = ExecuteToolWithGuardrails(session, call);
                if (call.name == "plan_work") {
                    plan_work_called = true;
                    final_content = result;
                }
                messages = context_.AddToolResult(messages, call.id, call.name, result);
                session.AddToolMessage(call.id, call.name, result);
            }
            if (plan_work_called) {
                break;
            }
        } else {
            if (requires_tool_guardrail && !tool_called && !guardrail_retry_used) {
                guardrail_retry_used = true;
                LOG_WARN("[agent] process_system_message blocked non-tool completion finish_reason={} session={}",
                         response.finish_reason,
                         session_key);
                AppendGuardrailUserMessage(messages, msg.content);
                continue;
            }
            if (tool_called && Trim(response.content).empty() && !post_tool_reminder_used) {
                post_tool_reminder_used = true;
                LOG_WARN("[agent] process_system_message empty post-tool response, appending reminder session={}", session_key);
                AppendPostToolReminder(messages);
                continue;
            }
            final_content = response.content;
            break;
        }
    }

    LOG_INFO("[agent] process_system_message completed tool_called={} guardrail_retry_used={} message_sent={} session={}",
             (tool_called ? "true" : "false"),
             (guardrail_retry_used ? "true" : "false"),
             (message_sent ? "true" : "false"),
             session_key);

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

std::string AgentLoop::ExecuteToolWithGuardrails(kabot::session::Session& session,
                                                     const kabot::providers::ToolCallRequest& call) {
    if (call.name == "file_edit") {
        auto path_it = call.arguments.find("path");
        if (path_it == call.arguments.end() || path_it->second.empty()) {
            return "Error: file_edit requires a path parameter";
        }
        if (!session.HasReadFile(path_it->second)) {
            LOG_WARN("[agent] file_edit blocked: {} was not read before editing session={}",
                     path_it->second, session.Key());
            return "Error: file must be read using read_file before editing with file_edit. Path: " + path_it->second;
        }
    }

    if (call.name == "agent") {
        if (auto* agent_tool = dynamic_cast<kabot::agent::tools::AgentTool*>(tools_.Get("agent"))) {
            agent_tool->SetSessionKey(session.Key());
        }
    }

    auto result = tools_.Execute(call.name, call.arguments);

    if (call.name == "read_file") {
        auto path_it = call.arguments.find("path");
        if (path_it != call.arguments.end() && !result.empty()) {
            if (result.rfind("Error:", 0) != 0) {
                session.RecordFileRead(path_it->second);
            }
        }
    }

    return result;
}

std::string AgentLoop::SpawnSubagent(const kabot::subagent::AgentSpawnInput& input,
                                      const std::string& session_key) {
    if (!subagent_service_) {
        throw std::runtime_error("subagent service not initialized");
    }

    kabot::subagent::SubagentContext parent_ctx;
    parent_ctx.agent_id = "main";
    parent_ctx.parent_session_id = session_key.empty() ? input.session_key : session_key;

    auto result = subagent_service_->Spawn(input, parent_ctx);
    if (result.type == "async_launched") {
        return "Spawned subagent " + result.agent_id + " as background task " + result.task_id;
    }

    std::ostringstream oss;
    oss << result.result << "\n\n";
    oss << "[Subagent Metadata]\n";
    oss << "agent_id: " << result.agent_id << "\n";
    oss << "total_tokens: " << result.total_tokens << "\n";
    oss << "tool_calls_count: " << result.tool_calls_count << "\n";
    oss << "duration_ms: " << result.duration_ms << "\n";
    if (!result.worktree_path.empty()) {
        oss << "worktree_path: " << result.worktree_path << "\n";
    }
    return oss.str();
}

}  // namespace kabot::agent
