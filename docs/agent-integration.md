# Clawhome Agent Integration Guide

本文档面向准备接入 Clawhome 的 agent 开发者。

目标是提供一份**只关注 agent 接入本身**的协议说明，让开发者或 AI 在不了解服务端内部实现的情况下，也可以实现一个兼容的 agent。

## Goal

一个兼容的 agent 需要做到：

- 建立认证过的 WebSocket 长连接
- 持续发送心跳
- 主动上报当前活动状态
- 接收 `command.dispatch` 命令
- 回传 `command.ack`
- 回传 `command.result`
- 在断线后自动重连

## Connection

agent 必须连接到以下 WebSocket 地址：

```text
ws://<host>:<port>/ws/agent?agentId=<agentId>&token=<token>
```

如果使用 TLS：

```text
wss://<host>/ws/agent?agentId=<agentId>&token=<token>
```

### Required parameters

- `agentId`
  - agent 的唯一标识
- `token`
  - 与该 `agentId` 对应的认证令牌

### Authentication behavior

如果 `agentId` 或 `token` 无效，WebSocket 握手会失败。

常见表现为：

- 连接被拒绝
- 握手返回 `401 Unauthorized`

### Practical advice

- `agentId` 和 `token` 应由部署方提供
- 若它们可能包含 URL 保留字符，应先进行 URL 编码
- 重连时继续使用相同的 `agentId` 和 `token`

## Message contract

当前协议采用 JSON 文本消息。

## Messages sent by the agent

### Heartbeat

用于维持连接活性：

```json
{
  "type": "heartbeat"
}
```

建议：

- 每 5 到 10 秒发送一次
- 或者至少保证发送间隔小于对端活性超时时间的一半

### Activity update

用于上报 agent 当前状态：

```json
{
  "type": "activity.update",
  "activityStatus": "busy",
  "activitySummary": "Analyzing logs",
  "currentCommandId": "cmd_xxx",
  "reportedAt": "2026-03-14T03:00:00.000Z"
}
```

字段说明：

- `activityStatus`
  - 可选值：`idle`、`busy`、`error`
- `activitySummary`
  - 面向人类阅读的简要状态描述
- `currentCommandId`
  - 可选，当前关联的命令 ID
- `reportedAt`
  - 可选，ISO 时间戳

建议发送时机：

- 连接建立成功后发送一次 `idle`
- 开始执行命令时发送 `busy`
- 命令执行过程中，可继续发送 `busy`，并通过 `activitySummary` 细分阶段，例如“Received relay command”、“Processing relay command”、“Calling tools”、“Preparing reply”
- 命令执行失败时发送 `error`
- 命令完成后发送 `idle`

### Command acknowledge

收到 `command.dispatch` 后，应尽快发送：

```json
{
  "type": "command.ack",
  "commandId": "cmd_xxx"
}
```

这表示该命令已被 agent 接收并进入处理流程。

### Command result

用于回传执行进度和最终结果。

运行中：

```json
{
  "type": "command.result",
  "commandId": "cmd_xxx",
  "status": "running",
  "progress": 50
}
```

执行成功：

```json
{
  "type": "command.result",
  "commandId": "cmd_xxx",
  "status": "completed",
  "result": "ok",
  "progress": 100
}
```

执行失败：

```json
{
  "type": "command.result",
  "commandId": "cmd_xxx",
  "status": "failed",
  "error": "command execution failed"
}
```

字段说明：

- `commandId`
  - 必须与收到的命令 ID 完全一致
- `status`
  - 可选值：`running`、`completed`、`failed`
- `progress`
  - 可选，数字进度
- `result`
  - 可选，成功时的结果摘要
- `error`
  - 可选，失败时的错误信息

## Messages received by the agent

### Command dispatch

agent 将接收到如下命令消息：

```json
{
  "type": "command.dispatch",
  "commandId": "cmd_xxx",
  "agentId": "agent1",
  "payload": "status",
  "createdAt": "2026-03-14T03:00:00.000Z"
}
```

字段说明：

- `commandId`
  - 当前命令的唯一 ID
- `agentId`
  - 目标 agent ID
- `payload`
  - 需要在本地执行的命令内容
- `createdAt`
  - 命令创建时间

## Recommended command flow

推荐实现如下处理顺序：

1. 建立 WebSocket 连接
2. 启动心跳定时器
3. 发送一次 `activity.update`，状态为 `idle`
4. 等待 `command.dispatch`
5. 收到命令后立即发送 `command.ack`
6. 发送 `activity.update`，状态为 `busy`
7. 启动本地执行
8. 如有需要，发送 `command.result`，状态为 `running`
9. 如需展示更细粒度进度，继续发送 `activity.update`，状态保持 `busy`，但更新 `activitySummary`
10. 执行成功后发送 `command.result`，状态为 `completed`
11. 执行失败后发送 `command.result`，状态为 `failed`
12. 最后发送 `activity.update`，状态为 `idle` 或 `error`

## Reconnect behavior

断线后，agent 应自动重连。

建议：

- 使用指数退避或有上限的递增退避
- 重连成功后重新启动心跳
- 重连成功后重新上报当前状态
- 不要假设断线前的会话状态仍然有效

## Minimal pseudocode

```text
load config
build websocket url /ws/agent?agentId=...&token=...
connect websocket

on open:
  start heartbeat timer
  send activity.update idle

on message:
  parse json
  if type == command.dispatch:
    send command.ack
    send activity.update busy
    try:
      send command.result running
      result = execute locally(payload)
      send command.result completed
      send activity.update idle
    catch err:
      send command.result failed
      send activity.update error

on close:
  stop heartbeat timer
  reconnect with backoff
```

## Validation checklist

完成接入后，建议至少验证以下几点：

- agent 能成功建立 WebSocket 连接
- 心跳持续发送且连接不会很快失活
- 能正确接收 `command.dispatch`
- 收到命令后能立即发送 `command.ack`
- 命令执行期间能发送 `running`
- 命令完成后能发送 `completed`
- 命令失败时能发送 `failed`
- 活动状态能在 `idle`、`busy`、`error` 间正确切换
- 断线后能自动重连并恢复心跳

## Common failure cases

### WebSocket cannot connect

检查：

- URL 路径是否是 `/ws/agent`
- `agentId` 是否正确
- `token` 是否正确
- 是否使用了错误的协议、主机或端口
- 查询参数是否需要 URL 编码

### Agent goes offline or stale

检查：

- 是否持续发送 `heartbeat`
- 心跳间隔是否过长
- 重连后是否重新启动了心跳定时器
- 是否把长时间执行任务阻塞在 WebSocket 事件线程里

### Commands are received but status does not update

检查：

- 是否发送了 `command.ack`
- `command.result.commandId` 是否与下发的 `commandId` 一致
- 最终是否发送了 `completed` 或 `failed`

### Dashboard state looks wrong

检查：

- 是否在关键状态切换时发送了 `activity.update`
- `activityStatus` 是否只使用 `idle`、`busy`、`error`
- `activitySummary` 是否足够简洁明确，并能体现执行阶段变化

## Implementation guidance

建议将 agent 分成以下几层：

- **Connection manager**
  - 负责连接、重连、心跳
- **Protocol layer**
  - 负责 JSON 编解码和消息分发
- **Command bridge**
  - 把 `payload` 转换为本地实际执行逻辑
- **Status reporter**
  - 负责发送 `activity.update`、`command.ack`、`command.result`

这样做的好处：

- 协议层清晰
- 本地执行逻辑与传输层解耦
- 更容易调试和扩展

## Good implementation rule

把 relay 协议层做窄，把本地执行层做独立。

也就是说：

- WebSocket 层只负责收发和重连
- 协议层只负责解析和构造消息
- 真正的本地执行逻辑放在单独的适配层
- 不要在 WebSocket 回调里执行阻塞式长任务
