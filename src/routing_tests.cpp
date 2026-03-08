#include "agent/agent_loop.hpp"
#include "agent/agent_registry.hpp"
#include "agent/tools/cron.hpp"
#include "cron/cron_service.hpp"

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

    return config;
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

}  // namespace

int main() {
    TestResolveAgentNameFromBindingAgent();
    TestResolveAgentNameFromExplicitMetadataOverride();
    TestSessionKeyIsolation();
    TestHandleInboundPreservesRoutingFields();
    TestCronToolCapturesAgentChannelAndToContext();
    TestMessageOnlyToolProfileRegistersOnlyMessageTool();
    std::cout << "routing_tests passed" << std::endl;
    return 0;
}
