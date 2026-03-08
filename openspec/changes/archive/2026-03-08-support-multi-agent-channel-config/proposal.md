## Why

当前配置模型默认一个部署只有一个 agent，并且每种 channel 只有一个固定实例。这限制了同一进程内同时服务多个业务入口、将不同会话路由到不同 agent，以及为不同 agent 隔离 workspace 的能力。

现在需要支持多个 channel 实例同时存在，并让每个 channel 实例绑定一个独立 agent，且每个 agent 可以绑定不同 workspace，从而满足多账号、多场景和隔离执行环境的使用方式。

## What Changes

- 引入可命名的 `agents` 配置项，支持定义多个 agent 实例，并为每个 agent 单独配置 `workspace`、agent 参数及工具档位。
- 引入可命名的 `channels` 实例配置，支持同一 channel 类型存在多个实例，例如多个 Telegram Bot 或多个 Lark 应用。
- 为 channel 增加单值 agent 绑定配置，使每个 channel 实例通过 `binding.agent` 明确绑定一个目标 agent。
- 调整启动与运行时模型，支持同时创建多个 channel 实例和多个 agent 实例，并保持现有消息总线语义。
- **BREAKING**：现有单一 `agents.defaults` 与固定 `channels.telegram` / `channels.lark` 配置模型将迁移为多实例配置模型，需要兼容旧配置或提供迁移方案。

## Capabilities

### New Capabilities
- `multi-agent-routing`: Support configuring multiple named agents and multiple channel instances, where each channel instance binds exactly one agent and each agent can use an independent workspace and tool profile.

### Modified Capabilities
- None.

## Impact

- Affected code:
  - `src/config/config_schema.hpp`
  - `src/config/config_loader.cpp`
  - `src/channels/channel_manager.*`
  - `src/agent/agent_loop.*`
  - `src/cli/commands.cpp`
  - message bus event/message routing paths
- Affected behavior:
  - Configuration file structure and environment variable mapping
  - Channel startup, registration, and outbound dispatch
  - Agent instance creation and session/workspace ownership
- Operational impact:
  - Existing deployments may require config migration or compatibility fallback
  - Documentation and example config need updates
