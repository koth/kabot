#include "agent/agent_loop.hpp"
#include "agent/agent_registry.hpp"
#include "agent/tools/cron.hpp"
#include "cron/cron_service.hpp"
#include "relay/relay_manager.hpp"
#include "task/task_runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace {

class StubProvider : public kabot::providers::LLMProvider {
public:
    kabot::providers::LLMResponse Chat(const std::vector<kabot::providers::Message>&,
                                       const std::vector<kabot::providers::ToolDefinition>&,
                                       const std::string&,
                                       int,
                                       double) override {
        kabot::providers::LLMResponse response{};
        response.content = "stub";
        return response;
    }

    std::string GetDefaultModel() const override {
        return "stub-model";
    }
};

class SequenceProvider : public kabot::providers::LLMProvider {
public:
    explicit SequenceProvider(std::vector<kabot::providers::LLMResponse> responses)
        : responses_(std::move(responses)) {}

    kabot::providers::LLMResponse Chat(const std::vector<kabot::providers::Message>&,
                                       const std::vector<kabot::providers::ToolDefinition>&,
                                       const std::string&,
                                       int,
                                       double) override {
        if (responses_.empty()) {
            kabot::providers::LLMResponse response{};
            response.content = "fallback";
            return response;
        }
        auto response = responses_.front();
        responses_.erase(responses_.begin());
        return response;
    }

    std::string GetDefaultModel() const override {
        return "stub-model";
    }

private:
    std::vector<kabot::providers::LLMResponse> responses_;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[routing_tests] " << message << std::endl;
        std::exit(1);
    }
}

kabot::config::Config BuildConfig() {
    kabot::config::Config config{};

    kabot::config::AgentInstanceConfig ops{};
    ops.name = "ops-agent";
    ops.workspace = (std::filesystem::temp_directory_path() / "kabot_routing_tests_ops").string();
    config.agents.instances.push_back(ops);

    kabot::config::AgentInstanceConfig sales{};
    sales.name = "sales-agent";
    sales.workspace = (std::filesystem::temp_directory_path() / "kabot_routing_tests_sales").string();
    config.agents.instances.push_back(sales);

    kabot::config::ChannelInstanceConfig telegram{};
    telegram.name = "telegram_ops";
    telegram.type = "telegram";
    telegram.enabled = true;
    telegram.binding.agent = "ops-agent";
    telegram.telegram.name = telegram.name;
    telegram.telegram.enabled = true;
    telegram.telegram.token = "token-ops";
    config.channels.instances.push_back(telegram);

    kabot::config::ChannelInstanceConfig lark{};
    lark.name = "lark_sales";
    lark.type = "lark";
    lark.enabled = true;
    lark.binding.agent = "sales-agent";
    lark.lark.name = lark.name;
    lark.lark.enabled = true;
    lark.lark.app_id = "app-id";
    lark.lark.app_secret = "app-secret";
    config.channels.instances.push_back(lark);

    kabot::config::ChannelInstanceConfig qqbot{};
    qqbot.name = "qqbot_ops";
    qqbot.type = "qqbot";
    qqbot.enabled = true;
    qqbot.binding.agent = "ops-agent";
    qqbot.qqbot.name = qqbot.name;
    qqbot.qqbot.enabled = true;
    qqbot.qqbot.app_id = "qq-app-id";
    qqbot.qqbot.client_secret = "qq-client-secret";
    config.channels.instances.push_back(qqbot);

    return config;
}

kabot::config::Config BuildTaskRuntimeConfig() {
    auto config = BuildConfig();
    config.task_system.enabled = true;
    config.task_system.poll_interval_s = 60;
    config.agents.defaults.workspace =
        (std::filesystem::temp_directory_path() / "kabot_task_runtime_defaults").string();

    kabot::config::RelayManagedAgentConfig relay_agent{};
    relay_agent.name = "relay-ops";
    relay_agent.enabled = true;
    relay_agent.local_agent = "ops-agent";
    relay_agent.agent_id = "agent-ops";
    relay_agent.token = "relay-token";
    relay_agent.host = "127.0.0.1";
    relay_agent.port = 18080;
    relay_agent.scheme = "http";
    relay_agent.path = "/ws/agent";
    relay_agent.reconnect_initial_delay_ms = 1000;
    relay_agent.reconnect_max_delay_ms = 2000;
    relay_agent.heartbeat_interval_s = 10;
    config.relay.managed_agents.push_back(relay_agent);
    return config;
}

std::size_t WaitingTaskCount(const kabot::task::TaskRuntime& runtime) {
    const auto state = nlohmann::json::parse(runtime.DumpStateJson(), nullptr, false);
    Expect(state.is_object(), "expected task runtime state dump to be JSON object");
    Expect(state.contains("waitingTasks"), "expected task runtime state to contain waitingTasks");
    Expect(state["waitingTasks"].is_array(), "expected waitingTasks to be an array");
    return state["waitingTasks"].size();
}

void TestResolveAgentNameFromBindingAgent() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    const auto config = BuildConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);

    kabot::bus::InboundMessage msg{};
    msg.channel = "telegram";
    msg.channel_instance = "telegram_ops";
    msg.chat_id = "chat-1";

    const auto resolved = registry.ResolveAgentName(msg);
    Expect(resolved == "ops-agent", "expected channel binding.agent to resolve ops-agent");
}

void TestResolveAgentNameFromQQBotBindingAgent() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    const auto config = BuildConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);

    kabot::bus::InboundMessage msg{};
    msg.channel = "qqbot";
    msg.channel_instance = "qqbot_ops";
    msg.chat_id = "group-openid-1";

    const auto resolved = registry.ResolveAgentName(msg);
    Expect(resolved == "ops-agent", "expected qqbot binding.agent to resolve ops-agent");
}

void TestResolveAgentNameFromExplicitMetadataOverride() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    const auto config = BuildConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);

    kabot::bus::InboundMessage msg{};
    msg.channel = "telegram";
    msg.channel_instance = "telegram_ops";
    msg.agent_name = "sales-agent";
    msg.chat_id = "chat-2";

    const auto resolved = registry.ResolveAgentName(msg);
    Expect(resolved == "sales-agent", "expected explicit valid agent_name to override binding.agent");
}

void TestSessionKeyIsolation() {
    kabot::bus::InboundMessage ops{};
    ops.channel = "telegram";
    ops.channel_instance = "telegram_ops";
    ops.agent_name = "ops-agent";
    ops.chat_id = "same-chat";

    kabot::bus::InboundMessage sales{};
    sales.channel = "lark";
    sales.channel_instance = "lark_sales";
    sales.agent_name = "sales-agent";
    sales.chat_id = "same-chat";

    Expect(ops.SessionKey() != sales.SessionKey(), "expected session keys to differ across channel instance and agent");
}

void TestHandleInboundPreservesRoutingFields() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    const auto config = BuildConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);

    kabot::bus::InboundMessage msg{};
    msg.channel = "telegram";
    msg.channel_instance = "telegram_ops";
    msg.chat_id = "chat-3";
    msg.content = "hello";

    const auto outbound = registry.HandleInbound(msg);
    Expect(outbound.channel_instance == "telegram_ops", "expected outbound to preserve channel_instance");
    Expect(outbound.agent_name == "ops-agent", "expected outbound to resolve bound agent");
    Expect(outbound.chat_id == "chat-3", "expected outbound to preserve chat_id");
}

void TestProcessDirectRoutesToNamedAgent() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    const auto config = BuildConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);

    const auto result = registry.ProcessDirect("ops-agent", "hello from relay", "relay:ops-agent:cmd-1");
    Expect(result == "stub", "expected ProcessDirect to route relay command to named local agent");
}

void TestProcessDirectRejectsUnknownAgent() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    const auto config = BuildConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);

    const auto result = registry.ProcessDirect("missing-agent", "hello from relay", "relay:missing-agent:cmd-1");
    Expect(result == "No agent is configured to handle this request.",
           "expected ProcessDirect to reject unknown relay local agent");
}

void TestCronToolCapturesAgentChannelAndToContext() {
    const auto store_path = std::filesystem::temp_directory_path() / "kabot_routing_tests_cron" / "store.json";
    std::filesystem::remove(store_path);
    std::filesystem::create_directories(store_path.parent_path());

    kabot::cron::CronService cron(store_path);
    kabot::agent::tools::CronTool tool(&cron);
    tool.SetContext("ops-agent", "telegram_ops", "chat-9");

    std::unordered_map<std::string, std::string> params;
    params["action"] = "add";
    params["message"] = "follow up";
    params["every_seconds"] = "60";

    const auto result = tool.Execute(params);
    const auto json = nlohmann::json::parse(result, nullptr, false);
    Expect(json.is_object(), "expected cron add to return JSON object");

    const auto jobs = cron.ListJobs(true);
    Expect(jobs.size() == 1, "expected exactly one cron job to be created");
    Expect(jobs.front().payload.agent == "ops-agent", "expected cron payload.agent to capture current agent");
    Expect(jobs.front().payload.channel == "telegram_ops", "expected cron payload.channel to capture current channel instance");
    Expect(jobs.front().payload.to == "chat-9", "expected cron payload.to to capture current chat target");
}

void TestMessageOnlyToolProfileRegistersOnlyMessageTool() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    kabot::config::AgentDefaults agent_config{};
    agent_config.tool_profile = "message_only";
    agent_config.workspace = (std::filesystem::temp_directory_path() / "kabot_message_only_agent").string();
    kabot::config::QmdConfig qmd{};

    kabot::agent::AgentLoop agent_loop(
        bus,
        provider,
        agent_config.workspace,
        agent_config,
        qmd,
        nullptr);

    auto tools = agent_loop.RegisteredTools();
    std::sort(tools.begin(), tools.end());
    Expect(tools.size() == 1, "expected message_only tool profile to register exactly one tool");
    Expect(tools.front() == "message", "expected message_only tool profile to keep only the message tool");
}

void TestProcessDirectEmitsObservedOutboundMessage() {
    kabot::bus::MessageBus bus;

    kabot::providers::LLMResponse tool_call_response{};
    tool_call_response.content = "asking user";
    tool_call_response.tool_calls.push_back({
        "call-1",
        "message",
        {
            {"content", "Please confirm deployment target."}
        }
    });
    kabot::providers::LLMResponse final_response{};
    final_response.content = "Done";

    SequenceProvider provider({tool_call_response, final_response});
    kabot::config::AgentDefaults agent_config{};
    agent_config.workspace = (std::filesystem::temp_directory_path() / "kabot_process_direct_observer").string();
    kabot::config::QmdConfig qmd{};

    kabot::agent::AgentLoop agent_loop(
        bus,
        provider,
        agent_config.workspace,
        agent_config,
        qmd,
        nullptr);

    kabot::bus::OutboundMessage observed{};
    bool observed_called = false;
    const auto result = agent_loop.ProcessDirect(
        "ask the user to confirm",
        "task:test:1",
        {},
        {"telegram", "telegram_ops", "chat-55"},
        [&](const kabot::bus::OutboundMessage& msg) {
            observed_called = true;
            observed = msg;
        });

    Expect(observed_called, "expected ProcessDirect outbound observer to be called when message tool sends");
    Expect(observed.content == "Please confirm deployment target.",
           "expected outbound observer to receive message tool content");
    Expect(observed.chat_id == "chat-55", "expected outbound observer to receive target chat_id");
    Expect(result == "Done", "expected ProcessDirect to continue after tool call and return final result");
}

void TestTaskRuntimeRecordsAndResumesWaitingTask() {
    kabot::bus::MessageBus bus;

    kabot::providers::LLMResponse waiting_response{};
    waiting_response.content = "Resumed successfully";
    SequenceProvider provider({waiting_response});

    auto config = BuildTaskRuntimeConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);
    kabot::relay::RelayManager relay(config, registry);
    kabot::task::TaskRuntime runtime(config, registry, relay, nullptr);

    kabot::bus::InboundMessage inbound{};
    inbound.channel = "telegram";
    inbound.channel_instance = "telegram_ops";
    inbound.agent_name = "ops-agent";
    inbound.chat_id = "chat-77";
    inbound.content = "original question";

    kabot::bus::OutboundMessage outbound{};
    outbound.channel = "telegram";
    outbound.channel_instance = "telegram_ops";
    outbound.agent_name = "ops-agent";
    outbound.chat_id = "chat-77";
    outbound.content = "Please confirm whether I should continue.";

    runtime.ObserveInboundResult(inbound, outbound);
    const auto state_after_waiting = nlohmann::json::parse(runtime.DumpStateJson(), nullptr, false);
    Expect(state_after_waiting.is_object(), "expected task runtime state dump to be JSON object");
    Expect(state_after_waiting.contains("waitingTasks"), "expected task runtime state to contain waitingTasks");
    Expect(state_after_waiting["waitingTasks"].is_array(), "expected waitingTasks to be an array");
    Expect(state_after_waiting["waitingTasks"].size() == 1, "expected exactly one waiting task to be recorded");

    kabot::bus::InboundMessage reply{};
    reply.channel = "telegram";
    reply.channel_instance = "telegram_ops";
    reply.agent_name = "ops-agent";
    reply.chat_id = "chat-77";
    reply.content = "go ahead";

    kabot::bus::OutboundMessage resumed{};
    const auto intercepted = runtime.HandleInbound(reply, resumed);
    Expect(intercepted, "expected task runtime to intercept reply for waiting task");
    Expect(resumed.content == "Resumed successfully", "expected waiting task reply to resume execution");

    const auto state_after_resume = nlohmann::json::parse(runtime.DumpStateJson(), nullptr, false);
    Expect(state_after_resume["waitingTasks"].empty(), "expected waiting task to be cleared after resume");
}

void TestTaskRuntimeDoesNotRecordCompletedReplyAsWaiting() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    auto config = BuildTaskRuntimeConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);
    kabot::relay::RelayManager relay(config, registry);
    kabot::task::TaskRuntime runtime(config, registry, relay, nullptr);

    kabot::bus::InboundMessage inbound{};
    inbound.channel = "telegram";
    inbound.channel_instance = "telegram_ops";
    inbound.agent_name = "ops-agent";
    inbound.chat_id = "chat-88";

    kabot::bus::OutboundMessage outbound{};
    outbound.channel = "telegram";
    outbound.channel_instance = "telegram_ops";
    outbound.agent_name = "ops-agent";
    outbound.chat_id = "chat-88";
    outbound.content = "Task completed successfully.";

    runtime.ObserveInboundResult(inbound, outbound);
    Expect(WaitingTaskCount(runtime) == 0, "expected completed reply not to create waiting task");
}

void TestTaskRuntimeClearsWaitingTaskOnNonWaitingReply() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    auto config = BuildTaskRuntimeConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);
    kabot::relay::RelayManager relay(config, registry);
    kabot::task::TaskRuntime runtime(config, registry, relay, nullptr);

    kabot::bus::InboundMessage inbound{};
    inbound.channel = "telegram";
    inbound.channel_instance = "telegram_ops";
    inbound.agent_name = "ops-agent";
    inbound.chat_id = "chat-89";

    kabot::bus::OutboundMessage waiting{};
    waiting.channel = "telegram";
    waiting.channel_instance = "telegram_ops";
    waiting.agent_name = "ops-agent";
    waiting.chat_id = "chat-89";
    waiting.content = "Please confirm whether I should continue.";

    runtime.ObserveInboundResult(inbound, waiting);
    Expect(WaitingTaskCount(runtime) == 1, "expected waiting reply to create waiting task");

    kabot::bus::OutboundMessage final_reply{};
    final_reply.channel = "telegram";
    final_reply.channel_instance = "telegram_ops";
    final_reply.agent_name = "ops-agent";
    final_reply.chat_id = "chat-89";
    final_reply.content = "I have completed the task.";

    runtime.ObserveInboundResult(inbound, final_reply);
    Expect(WaitingTaskCount(runtime) == 0, "expected non-waiting reply to clear prior waiting task");
}

void TestTaskRuntimeHandleInboundReturnsFalseWhenNoWaitingTask() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    auto config = BuildTaskRuntimeConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);
    kabot::relay::RelayManager relay(config, registry);
    kabot::task::TaskRuntime runtime(config, registry, relay, nullptr);

    kabot::bus::InboundMessage reply{};
    reply.channel = "telegram";
    reply.channel_instance = "telegram_ops";
    reply.agent_name = "ops-agent";
    reply.chat_id = "chat-miss";
    reply.content = "hello";

    kabot::bus::OutboundMessage outbound{};
    const auto intercepted = runtime.HandleInbound(reply, outbound);
    Expect(!intercepted, "expected HandleInbound to return false when no waiting task matches");
}

void TestTaskRuntimeStateDumpContainsCoreFields() {
    kabot::bus::MessageBus bus;
    StubProvider provider;
    auto config = BuildTaskRuntimeConfig();
    kabot::agent::AgentRegistry registry(bus, provider, config, nullptr);
    kabot::relay::RelayManager relay(config, registry);
    kabot::task::TaskRuntime runtime(config, registry, relay, nullptr);

    const auto state = nlohmann::json::parse(runtime.DumpStateJson(), nullptr, false);
    Expect(state.is_object(), "expected runtime state dump to be JSON object");
    Expect(state.value("enabled", false), "expected runtime state dump to reflect enabled task system");
    Expect(state.contains("pollIntervalS"), "expected runtime state dump to contain pollIntervalS");
    Expect(state.contains("dailySummaryHourLocal"), "expected runtime state dump to contain dailySummaryHourLocal");
    Expect(state.contains("dailySummaries"), "expected runtime state dump to contain dailySummaries");
    Expect(state.contains("waitingTasks"), "expected runtime state dump to contain waitingTasks");
}

}  // namespace

int main() {
    TestResolveAgentNameFromBindingAgent();
    TestResolveAgentNameFromQQBotBindingAgent();
    TestResolveAgentNameFromExplicitMetadataOverride();
    TestSessionKeyIsolation();
    TestHandleInboundPreservesRoutingFields();
    TestProcessDirectRoutesToNamedAgent();
    TestProcessDirectRejectsUnknownAgent();
    TestCronToolCapturesAgentChannelAndToContext();
    TestMessageOnlyToolProfileRegistersOnlyMessageTool();
    TestProcessDirectEmitsObservedOutboundMessage();
    TestTaskRuntimeRecordsAndResumesWaitingTask();
    TestTaskRuntimeDoesNotRecordCompletedReplyAsWaiting();
    TestTaskRuntimeClearsWaitingTaskOnNonWaitingReply();
    TestTaskRuntimeHandleInboundReturnsFalseWhenNoWaitingTask();
    TestTaskRuntimeStateDumpContainsCoreFields();
    std::cout << "routing_tests passed" << std::endl;
    return 0;
}
