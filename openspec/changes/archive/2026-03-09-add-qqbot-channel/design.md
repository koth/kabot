## Context

Kabot 当前的 channel 接入能力主要围绕 Telegram 与 Lark 展开。配置层已经支持 `channels.instances` 多实例模型，运行时通过 `ChannelBase`、`ChannelManager` 和消息总线把不同 channel 的入站消息统一路由到 agent，再由 agent 输出通过对应 channel 实例回发。

要接入 QQBot，最直接的路径是沿用现有 channel 抽象，并参考 `qqbot_cpp` SDK 提供的两类能力：
- WebSocket / 事件流方式接收消息并回复
- OpenAPI 方式发送频道消息、私信消息、C2C 消息、群聊消息，以及上传文件/图片

这项改动横跨配置、运行时、构建依赖、消息模型映射和测试，因此需要一个明确设计来约束首版范围与扩展方式。

## Goals / Non-Goals

**Goals:**
- 支持在 `channels.instances` 中声明 `type: "qqbot"` 的 channel instance。
- 为 QQBot 增加独立配置结构，包括 AppID、ClientSecret/Token、Sandbox、Intents 等首版必要参数。
- 新增 `QQBotChannel`，复用 `ChannelBase` 接口完成启动、停止、收消息、发消息。
- 将 QQBot 入站事件规范化为 `InboundMessage`，保留足够的 metadata 以支持会话和回复。
- 支持首版出站文本消息，并为私信、频道、C2C、群聊等不同目标类型建立明确的路由规则。
- 补充配置验证、最小运行时测试和文档说明。

**Non-Goals:**
- 不在首版中追求覆盖 `qqbot_cpp` 的全部富媒体能力。
- 不在首版中实现 QQBot 专属工具或独立 agent 路由策略。
- 不重构现有 bus、session 或 agent 架构。
- 不承诺首版就兼容所有 QQ 平台事件类型，仅覆盖 SDK 当前稳定示例能支持的文本消息链路。

## Decisions

### 1. 继续沿用现有 channel 抽象，新增 `QQBotChannel`
QQBot 接入将实现为新的 `ChannelBase` 子类，而不是在 `ChannelManager` 中硬编码特殊流程。

原因：
- 与 Telegram/Lark 一致，便于复用启动/停止/出站调度模型。
- 不需要改动 agent 和 bus 的主流程。
- 后续若增加 Discord、Slack 等 channel，也可沿用相同模式。

备选方案：直接在 `commands.cpp` 或 `ChannelManager` 中加入 QQBot 专属逻辑。该方案会让 channel 生命周期与路由逻辑耦合，扩展性较差，因此不采用。

### 2. 在配置模型中新增 `QQBotConfig`，并把它并入 `ChannelInstanceConfig`
计划新增：
- `QQBotConfig`
  - `name`
  - `enabled`
  - `app_id`
  - `client_secret`
  - `token`（兼容旧 Token 模式或 SDK fallback）
  - `sandbox`
  - `intents`
  - `skip_tls_verify`
  - `allow_from`
  - `binding`
- `ChannelsConfig::qqbot` 作为旧式单实例兼容入口
- `ChannelInstanceConfig::qqbot` 作为规范化后的实例配置

原因：
- 与 `TelegramConfig` / `LarkConfig` 一致，便于 loader 和 normalize 流程复用。
- 保留一个兼容层，后续可以决定是否提供类似旧单实例配置映射。

备选方案：把 QQBot 所有字段直接平铺到 `ChannelInstanceConfig`。该方案会让 channel 特有字段污染通用结构，因此不采用。

### 3. 用 metadata 显式区分 QQBot 的消息目标类型
QQBot 发送接口区分频道消息、私信、C2C、群聊等类型，不能仅依赖单一 `chat_id`。

设计上：
- 入站时把事件来源映射到统一的 `chat_id`，同时在 metadata 中补充：
  - `qqbot_chat_type`
  - `qqbot_channel_id`
  - `qqbot_guild_id`
  - `qqbot_group_openid`
  - `qqbot_user_openid`
  - `qqbot_message_id`
- 出站时优先根据 metadata 中的 QQBot 目标类型与标识，选择对应 `qqbot_cpp` API。
- 对于“回复当前会话”的默认路径，`QQBotChannel` 需要维护最小的 message-to-target 映射缓存，类似 Telegram/Lark 当前为 reply 或回发保留的上下文。

原因：
- 统一 `chat_id` 便于 session 与 cron 复用。
- metadata 能保留平台特有上下文，不破坏通用消息结构。

备选方案：扩展 bus 的顶层字段来表达所有 QQBot 目标类型。这样会把平台细节带到所有 channel，不采用。

### 4. 首版优先支持文本消息，附件作为可扩展能力保留接口
首版以“收到文本 -> agent 处理 -> 发文本”作为最小可用闭环。若 `qqbot_cpp` 的示例表明文件/图片上传能力成熟，则可以在设计里保留 metadata 和辅助方法，但不要求第一版全部打通。

原因：
- 文本闭环是验证 channel 接入正确性的核心。
- QQBot 目标类型已经增加复杂度，应避免首版同时把富媒体与回帖/引用逻辑全部做满。

备选方案：一开始就完整支持文件、图片、富媒体。风险是实现面过大、验证成本高，因此不作为首版目标。

### 5. 构建层通过可选依赖接入 `qqbot_cpp`
`src/CMakeLists.txt` 需要引入 `qqbot_cpp` 的头文件与链接配置，并尽量保持为可选组件：
- 若依赖存在，则编译 QQBot channel
- 若依赖缺失，则在配置或构建期给出明确错误

原因：
- 降低对现有 Telegram/Lark 用户的影响。
- 为 Windows 本地构建留出更清晰的依赖处理路径。

备选方案：直接把 `qqbot_cpp` 作为强依赖。这样会提高首次构建门槛，因此优先考虑可选或条件编译。

## Risks / Trade-offs

- [QQBot 目标类型多于现有 channel] → 通过 metadata + target cache 明确区分频道、私信、C2C、群聊路径。
- [SDK 依赖与构建方式可能复杂] → 在 README 和 CMake 中明确依赖接入步骤，并优先支持最小可编译路径。
- [不同事件结构映射到统一消息模型时丢失上下文] → 为 QQBot 增加平台专属 metadata 字段，并在发送时优先消费这些字段。
- [首版只做文本可能与用户对“完整接入”的期待不一致] → 在文档中明确首版范围，并保留附件/富媒体扩展点。
- [reply 依赖 message id 上下文，进程重启后可能丢失] → 首版接受内存级映射限制，后续若需要再评估持久化。

## Migration Plan

1. 扩展配置 schema / loader / validation，使 `qqbot` 成为合法 channel type。
2. 引入 `QQBotChannel` 并接入 `ChannelManager::RegisterInstance`。
3. 在 CMake 中接入 `qqbot_cpp` 依赖并保证主工程可以编译。
4. 打通最小消息闭环：接收文本、发布到 bus、agent 处理、发送文本回复。
5. 补充配置与 routing 测试，覆盖 channel type 校验和 QQBot 实例初始化/发送逻辑。
6. 更新 README，记录配置示例、依赖安装和已知限制。

## Open Questions

- `qqbot_cpp` 在当前项目中的最佳集成方式是子模块、vcpkg 还是本地路径依赖？
- 首版是否要求同时覆盖频道消息与 C2C/群聊消息，还是允许先只实现其中一类接入？
- 若 QQBot 出站需要严格依赖源消息 `msg_id`，是否需要把该字段统一沉淀到通用回复路径约定中？
- 是否需要像 Telegram 一样落地媒体文件到本地路径，还是先只透传 metadata？
