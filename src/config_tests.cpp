#include "config/config_loader.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[config_tests] " << message << std::endl;
        std::exit(1);
    }
}

void TestValidateConfigWithBindingAgent() {
    kabot::config::Config config{};

    kabot::config::AgentInstanceConfig agent{};
    agent.name = "ops-agent";
    agent.workspace = "workspace-ops";
    config.agents.instances.push_back(agent);

    kabot::config::ChannelInstanceConfig channel{};
    channel.name = "telegram_ops";
    channel.type = "telegram";
    channel.enabled = true;
    channel.binding.agent = "ops-agent";
    channel.telegram.name = channel.name;
    channel.telegram.enabled = true;
    channel.telegram.token = "token";
    config.channels.instances.push_back(channel);

    const auto errors = kabot::config::ValidateConfig(config);
    Expect(errors.empty(), "expected config with binding.agent to validate successfully");
}

void TestValidateConfigRejectsMissingBindingAgent() {
    kabot::config::Config config{};

    kabot::config::AgentInstanceConfig agent{};
    agent.name = "ops-agent";
    config.agents.instances.push_back(agent);

    kabot::config::ChannelInstanceConfig channel{};
    channel.name = "telegram_ops";
    channel.type = "telegram";
    channel.enabled = true;
    channel.telegram.name = channel.name;
    channel.telegram.enabled = true;
    channel.telegram.token = "token";
    config.channels.instances.push_back(channel);

    const auto errors = kabot::config::ValidateConfig(config);
    Expect(!errors.empty(), "expected missing binding.agent to fail validation");
}

void TestValidateConfigRejectsUnknownBindingAgent() {
    kabot::config::Config config{};

    kabot::config::AgentInstanceConfig agent{};
    agent.name = "ops-agent";
    config.agents.instances.push_back(agent);

    kabot::config::ChannelInstanceConfig channel{};
    channel.name = "telegram_ops";
    channel.type = "telegram";
    channel.enabled = true;
    channel.binding.agent = "missing-agent";
    channel.telegram.name = channel.name;
    channel.telegram.enabled = true;
    channel.telegram.token = "token";
    config.channels.instances.push_back(channel);

    const auto errors = kabot::config::ValidateConfig(config);
    Expect(!errors.empty(), "expected unknown binding.agent to fail validation");
}

void TestValidateConfigRejectsUnsupportedToolProfile() {
    kabot::config::Config config{};

    kabot::config::AgentInstanceConfig agent{};
    agent.name = "ops-agent";
    agent.tool_profile = "everything";
    config.agents.instances.push_back(agent);

    kabot::config::ChannelInstanceConfig channel{};
    channel.name = "telegram_ops";
    channel.type = "telegram";
    channel.enabled = true;
    channel.binding.agent = "ops-agent";
    channel.telegram.name = channel.name;
    channel.telegram.enabled = true;
    channel.telegram.token = "token";
    config.channels.instances.push_back(channel);

    const auto errors = kabot::config::ValidateConfig(config);
    Expect(!errors.empty(), "expected unsupported toolProfile to fail validation");
}

void TestLegacyConfigCompatibility() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "kabot_config_tests";
    std::filesystem::create_directories(temp_dir);
    const auto config_path = temp_dir / "config.json";

    std::ofstream output(config_path);
    output << R"({
  "agents": {
    "defaults": {
      "workspace": "legacy-workspace",
      "model": "legacy-model"
    }
  },
  "channels": {
    "telegram": {
      "enabled": true,
      "token": "legacy-token",
      "allowFrom": ["12345"]
    }
  }
})";
    output.close();

    const auto config = kabot::config::LoadConfig(config_path);
    Expect(config.agents.instances.size() == 1, "expected legacy config to create one default agent instance");
    Expect(config.agents.instances.front().name == "default", "expected legacy default agent name");
    Expect(config.agents.instances.front().workspace == "legacy-workspace", "expected legacy workspace to be preserved");
    Expect(config.channels.instances.size() == 1, "expected legacy telegram config to create one channel instance");
    Expect(config.channels.instances.front().name == "telegram", "expected legacy telegram instance name");
    Expect(config.channels.instances.front().binding.agent == "default", "expected legacy channel to bind default agent");
}

}  // namespace

int main() {
    TestValidateConfigWithBindingAgent();
    TestValidateConfigRejectsMissingBindingAgent();
    TestValidateConfigRejectsUnknownBindingAgent();
    TestValidateConfigRejectsUnsupportedToolProfile();
    TestLegacyConfigCompatibility();
    std::cout << "config_tests passed" << std::endl;
    return 0;
}
