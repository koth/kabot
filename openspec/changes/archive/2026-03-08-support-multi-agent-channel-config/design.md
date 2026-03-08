## Context

当前系统的核心假设是“单 agent + 每种 channel 一个固定实例”。`Config` 中只有 `agents.defaults` 和固定字段 `channels.telegram` / `channels.lark`；`ChannelManager` 在启动时只创建固定的 Telegram 与 Lark channel；`AgentLoop` 只持有一个 `workspace_` 和一套 session/tool/memory 状态。

这使得系统无法在同一进程内同时承载多个业务 agent，也无法为不同接收账号对应不同 agent 并提供独立 workspace。新需求要求多个 channel 实例同时存在，且每个 channel 实例绑定一个独立 agent，并且每个 agent 可以绑定不同 workspace。该变更会横跨配置解析、运行时路由、会话归属和运维配置迁移，是一项跨模块架构调整。

## Goals / Non-Goals

**Goals:**
- 支持定义多个具名 agent，每个 agent 可以覆盖默认模型参数并指定独立 `workspace`。
- 支持每个 agent 配置明确的工具档位，例如 `full` 与 `message_only`。
- 支持定义多个具名 channel 实例，同一 channel 类型可出现多次。
- 支持每个 channel 实例通过单值绑定直接关联一个目标 agent。
- 保持单进程运行模型，允许多个 channel 和多个 agent 同时启动。
- 为旧配置提供兼容读取或可预测的迁移路径，避免一次性破坏现有部署。

**Non-Goals:**
- 不在本次设计中引入跨进程或分布式 agent 调度。
- 不在本次设计中新增复杂的基于内容语义的智能路由。
- 不重写现有消息总线为外部消息队列。
- 不要求所有 tool/provider 都支持 agent 级完全独立的实例化优化，本次以功能正确为主。

## Decisions

### 1. 配置模型改为“具名实例集合 + 默认值继承”
采用具名 map/list 的配置模型，而不是继续扩展固定字段：
- `agents.defaults` 保留为全局默认值。
- 新增 `agents.instances`（或等价具名集合），每个 agent 具备唯一 `name`，并可覆盖 `workspace`、`model`、`maxTokens` 等字段。
- `channels` 改为实例集合，每个 channel 实例具备：`name`、`type`、连接参数、`allowFrom`、`binding.agent` 单值绑定。

这样可以避免为每一种 channel 类型不断扩 schema 字段，也让未来扩展 Slack/Discord 等 channel 更自然。

备选方案：继续保留 `channels.telegram` / `channels.lark` 固定结构并在内部加数组。该方案对当前两种 channel 可行，但扩展性差，且会让不同 channel 类型的公共路由字段难以统一，因此不采用。

### 2. 新增 AgentRegistry，按 agent 名称持有长期存活的 AgentLoop 实例
运行时不再只构造一个 `AgentLoop`，而是引入 `AgentRegistry`（名称可实现时调整），在启动时根据配置创建多个具名 agent 实例，并按名称索引。

每个 agent 实例独立持有：
- workspace
- session manager
- memory store
- tool registry
- provider-facing agent config

这样可保证不同 agent 的会话上下文与工具执行目录互不干扰。

在工具层面，agent 配置新增 `toolProfile`，首版支持：
- `full`：保留现有完整工具集
- `message_only`：仅注册 `message` 工具

这样可以让某些 agent 只承担通知/转发职责，而不具备文件、命令、网页搜索或 cron 等额外能力。

备选方案：在每次处理消息时临时创建 `AgentLoop`。该方式实现简单，但会丢失会话状态、增加初始化开销，也不利于后续扩展，因此不采用。

### 3. channel 实例只负责接入，agent 选择通过显式路由完成
channel 不直接内嵌 agent 逻辑，而是负责把入站消息连同 `channel_instance`、`sender_id`、`chat_id` 等元数据投递到 bus。后续由路由层根据 channel 配置中的 `binding.agent` 直接选择目标 agent。

建议优先采用以下规则：
- 若入站消息显式携带 `agent_name` 且合法，则使用该 agent。
- 否则按 channel 实例的 `binding.agent` 直接路由。
- 若 `binding.agent` 缺失或引用不存在的 agent，则启动失败。

这样可以让“一个接收账号对应一个 agent”的模型保持直接、可预测，也更贴近实际部署语义。

### 4. 会话键必须包含 channel 实例与 agent 身份
现有 session key 主要基于 channel/chat/user 组合。引入多 agent 后，必须保证不同 agent 对同一 chat 的上下文隔离，因此会话键需要包含至少：
- channel 实例名
- agent 名称
- chat_id / sender_id

这样既能支持一个 channel 下多个 agent 并存，也能支持不同 channel 指向同一 agent 时共享或隔离策略的显式定义。

### 5. 出站消息需要带上 channel 实例标识，而不仅是 channel 类型
当前 `OutboundMessage.channel` 更像 `telegram` / `lark` 类型名。多实例 channel 后，这个字段应表达实际目标实例名，例如 `telegram_ops`、`lark_support`。`ChannelManager` 以实例名注册与查找 channel，避免同类 channel 相互覆盖。

### 6. 兼容旧配置，按单实例模型自动映射到新运行时结构
为了降低迁移成本，配置加载层应保留旧 JSON / 环境变量解析路径：
- 旧 `channels.telegram` / `channels.lark` 自动映射为默认实例，如 `telegram` / `lark`
- 旧单一 agent 配置映射为默认 agent，如 `default`
- 若显式使用新结构，则优先采用新结构

这样可以逐步迁移，而不是要求所有用户同步改配置。

## Risks / Trade-offs

- [配置复杂度上升] → 提供兼容旧配置、示例配置和启动时校验错误信息。
- [用户误以为一个 channel instance 支持多个 agent 选择] → 通过 `binding.agent` 单值模型和示例配置明确“一账号一 agent”。
- [不同 agent 的工具能力边界不清晰] → 通过 `toolProfile` 显式声明工具档位，并对非法值在启动时校验失败。
- [多个 agent 各自持有 session/memory/tools 带来更高内存占用] → 先保证隔离与正确性，后续再考虑按需懒加载。
- [环境变量模型难以表达多实例嵌套配置] → 将 JSON 配置文件作为主配置方式，环境变量仅保留默认 agent 与旧单实例兼容能力。
- [现有 bus 消息结构可能缺少路由字段] → 在设计中明确增加 agent/channel-instance 元数据，并在迁移阶段集中修改生产者与消费者。

## Migration Plan

1. 扩展配置 schema，定义具名 agent 与 channel 实例结构，同时保留旧结构。
2. 在配置加载层实现新旧模型归一化，输出统一运行时配置。
3. 引入 agent registry 与消息路由层，调整 `ChannelManager` 和 `commands.cpp` 启动逻辑。
4. 更新入站/出站消息结构与 session key 生成逻辑，确保 agent 隔离。
5. 为旧配置添加兼容测试，为新多实例配置添加集成测试。
6. 更新 README 与示例配置，说明旧配置兼容策略与推荐新格式。

回滚策略：若新模型发布后出现问题，可继续使用旧单实例配置路径运行；运行时也可通过仅配置一个默认 agent 和一个默认 channel 实例来退回旧行为。

## Open Questions

- agent 之间是否需要共享某些只读资源（例如 provider 客户端缓存），还是完全独立实例化？
- 出站消息是否需要显式指定 `agent` 字段用于审计与回溯，还是只保留 channel 实例即可？
- 环境变量是否要支持完整多实例配置，还是明确限制为旧兼容和少量默认项？
