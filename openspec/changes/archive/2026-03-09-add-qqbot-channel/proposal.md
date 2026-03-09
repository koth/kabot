## Why

当前系统已经支持 Telegram 与 Lark，但还无法接入 QQ 频道/QQ 机器人入口。这限制了 Kabot 在中文社区和现有 QQ 生态中的可用性，而仓库 `qqbot_cpp` 已经提供了可参考的 C++ SDK 与消息收发示例，具备推进接入的条件。

## What Changes

- 新增 `qqbot` channel 类型，使 Kabot 可以把 QQBot 作为新的消息接入与消息发送通道。
- 扩展配置模型与校验逻辑，允许在 `channels.instances` 中声明 `type: "qqbot"` 的实例，并配置接入 QQBot 所需的认证、沙箱和意图参数。
- 新增 `QQBotChannel` 运行时实现，复用现有 `ChannelBase` / `ChannelManager` / message bus 路径，把 QQBot 入站消息转换为统一的 `InboundMessage`，并把 agent 输出转换为 QQBot 出站消息。
- 定义首版支持范围：优先覆盖文本消息接收与回复，兼容频道/私信/群聊等 `qqbot_cpp` 已有示例能力；附件与富媒体可先按最小可用范围设计并明确后续扩展点。
- 更新构建与文档，说明 `qqbot_cpp` 依赖、配置项、运行方式和已知限制。

## Capabilities

### New Capabilities
- `qqbot-channel`: Support configuring, starting, receiving from, and sending messages through QQBot channel instances via the `qqbot_cpp` SDK.

### Modified Capabilities
- `multi-agent-routing`: Extend channel instance routing and validation so `channels.instances` can include `type: "qqbot"` alongside existing channel types.

## Impact

- Affected code:
  - `src/config/config_schema.hpp`
  - `src/config/config_loader.cpp`
  - `src/channels/channel_manager.*`
  - `src/channels/channel_base.*`
  - new `src/channels/qqbot_channel.*`
  - `src/CMakeLists.txt`
  - `src/README.md`
  - tests around config validation and channel routing
- External dependency:
  - `qqbot_cpp` SDK and its build/runtime requirements
- Runtime systems:
  - channel startup/shutdown lifecycle
  - inbound/outbound message routing
  - per-channel credential and intent configuration
