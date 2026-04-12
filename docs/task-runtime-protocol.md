# Kabot Task Runtime Protocol

本文档定义 **relay-server 与 kabot gateway task runtime** 之间的最小协议。

目标：

- relay-server 负责任务存储、分发、状态聚合
- kabot gateway 负责拉取任务、认领、在本地 agent 中串行执行、向真实用户发起确认、恢复执行、回写状态
- 后端开发者或大模型可以只看本文档实现兼容服务端

本文档刻意保持协议窄而明确，避免把 agent 内部实现细节泄露到服务端。

## Design goals

- 一个本地 agent 在任一时刻只处理 **一个 active task**
- 任务获取走 **HTTP pull + claim**，不要求服务端主动推送
- 日报上传继续沿用现有 HTTP 风格
- 认证统一使用 `Authorization: Bearer <token>`
- 所有时间统一使用 ISO 8601 UTC，例如 `2026-03-15T14:00:00Z`
- 所有请求与响应体均为 `application/json`

## Authentication

除非特别说明，所有 HTTP 请求都必须带：

```http
Authorization: Bearer <token>
Content-Type: application/json
```

其中：

- `agentId` 用于路径定位 agent
- `token` 用于认证该 agent

如果认证失败，服务端返回：

- `401 Unauthorized`
- 响应 body 建议包含明确错误信息

## Base resource model

所有任务接口都挂在：

```text
/api/agents/:agentId/tasks
```

## Task object

服务端返回给 gateway 的任务对象格式如下：

```json
{
  "taskId": "task_123",
  "title": "Investigate failing deployment",
  "instruction": "Check the latest deployment logs and summarize the failure.",
  "sessionKey": "task:alpha:task_123",
  "createdAt": "2026-03-15T13:58:00Z",
  "priority": "high",
  "interaction": {
    "channel": "telegram",
    "channelInstance": "telegram",
    "chatId": "123456789",
    "replyTo": "987654321"
  },
  "metadata": {
    "source": "ops-console",
    "requestedBy": "alice"
  }
}
```

字段说明：

- `taskId`
  - 必填，任务唯一 ID
- `title`
  - 可选，便于展示
- `instruction`
  - 必填，交给本地 agent 执行的正文
- `sessionKey`
  - 可选，若为空，gateway 应回退到 `task:<local_agent>:<taskId>`
- `createdAt`
  - 可选
- `priority`
  - 可选，可为 `low`、`medium`、`high`
- `interaction`
  - 可选，若存在，表示任务允许/需要与真实用户交互
- `interaction.channel`
  - 必填，例如 `telegram`
- `interaction.channelInstance`
  - 可选，若为空则等于 `channel`
- `interaction.chatId`
  - 必填，真实用户会话标识
- `interaction.replyTo`
  - 可选，用于服务端记录来源，不要求 gateway 必须消费
- `metadata`
  - 可选，自由扩展字段，gateway 可以透传但不依赖其语义

## Task status enum

服务端必须接受以下 `status`：

- `claimed`
- `running`
- `waiting_user`
- `completed`
- `failed`
- `canceled`

语义：

- `claimed`
  - 已被某个 gateway 成功认领，但尚未真正开始执行
- `running`
  - 本地 agent 已开始执行
- `waiting_user`
  - agent 需要真实用户补充确认/选择，执行暂停
- `completed`
  - 执行成功结束
- `failed`
  - 执行失败结束
- `canceled`
  - 服务端或本地放弃该任务

## 1. Claim next task

gateway 通过一个原子操作“领取下一条可执行任务”。

### Request

```http
POST /api/agents/:agentId/tasks/claim-next
Authorization: Bearer <token>
Content-Type: application/json
```

请求体：

```json
{
  "localAgent": "alpha",
  "claimedAt": "2026-03-15T14:00:00Z",
  "workerId": "gateway:alpha",
  "supportsInteraction": true
}
```

字段说明：

- `localAgent`
  - 必填，gateway 内部本地 agent 名称
- `claimedAt`
  - 必填，当前领取时间
- `workerId`
  - 可选，建议固定为 `gateway:<localAgent>`
- `supportsInteraction`
  - 必填，当前 runtime 是否支持等待用户确认场景

### Response: task claimed

HTTP `200 OK`

```json
{
  "found": true,
  "task": {
    "taskId": "task_123",
    "title": "Investigate failing deployment",
    "instruction": "Check the latest deployment logs and summarize the failure.",
    "sessionKey": "task:alpha:task_123",
    "createdAt": "2026-03-15T13:58:00Z",
    "priority": "high",
    "interaction": {
      "channel": "telegram",
      "channelInstance": "telegram",
      "chatId": "123456789"
    },
    "metadata": {
      "source": "ops-console"
    }
  }
}
```

### Response: no task

HTTP `200 OK`

```json
{
  "found": false
}
```

### Server requirements

- `claim-next` 必须是 **原子操作**
- 同一条任务不能被两个 gateway 同时认领
- 若 agent 当前不应再领取任务，服务端返回 `found=false`

## 2. Update task status

gateway 用此接口回写任务阶段变化。

### Request

```http
POST /api/agents/:agentId/tasks/:taskId/status
Authorization: Bearer <token>
Content-Type: application/json
```

请求体：

```json
{
  "status": "waiting_user",
  "message": "Please confirm whether I should proceed with deleting the old index.",
  "progress": 40,
  "reportedAt": "2026-03-15T14:01:00Z",
  "sessionKey": "task:alpha:task_123",
  "result": null,
  "error": null,
  "waitingUser": {
    "channel": "telegram",
    "channelInstance": "telegram",
    "chatId": "123456789",
    "question": "Please confirm whether I should proceed with deleting the old index."
  }
}
```

字段说明：

- `status`
  - 必填，见上面的状态枚举
- `message`
  - 可选，人类可读摘要
- `progress`
  - 可选，0-100
- `reportedAt`
  - 必填
- `sessionKey`
  - 可选，但建议回传
- `result`
  - 仅 `completed` 时建议填写
- `error`
  - 仅 `failed` 时建议填写
- `waitingUser`
  - 仅 `waiting_user` 时建议填写

### Response

HTTP `200 OK`

```json
{
  "ok": true
}
```

## 3. Resume waiting task acknowledgement

当 gateway 收到真实用户回复并决定恢复任务时，先告知服务端该任务从 `waiting_user` 切回 `running`。

这一步不需要单独接口，直接调用 **Update task status** 即可：

```json
{
  "status": "running",
  "message": "Resumed after user reply",
  "reportedAt": "2026-03-15T14:05:00Z",
  "sessionKey": "task:alpha:task_123"
}
```

## 4. Optional cancel check

如果后端希望支持任务取消，建议额外提供只读接口：

```http
GET /api/agents/:agentId/tasks/:taskId
Authorization: Bearer <token>
```

响应示例：

```json
{
  "taskId": "task_123",
  "status": "canceled"
}
```

当前 gateway 客户端 **不强依赖** 此接口，但服务端可以预留。

## 5. Daily summary upload

该接口已经在 kabot 客户端存在，实现时请保持兼容。

### Request

```http
POST /api/agents/:agentId/daily-summary
Authorization: Bearer <token>
Content-Type: application/json
```

请求体：

```json
{
  "summaryDate": "2026-03-15",
  "content": "...daily memory markdown content...",
  "reportedAt": "2026-03-15T14:00:00Z"
}
```

### Response

成功：`2xx`

失败：`4xx/5xx`，body 应尽量返回纯文本或 JSON 错误消息

## Recommended server-side state machine

推荐服务端任务状态机：

```text
queued
  -> claimed
  -> running
  -> waiting_user
  -> running
  -> completed
```

失败分支：

```text
queued
  -> claimed
  -> running
  -> failed
```

取消分支：

```text
queued|claimed|running|waiting_user
  -> canceled
```

## Client behavior expected by this protocol

gateway 客户端应按以下顺序工作：

1. 每个 relay-managed local agent 独立轮询 `claim-next`
2. 若 `found=false`，等待下一个轮询周期
3. 若拿到任务：
   - 立即回写 `claimed`
   - 再回写 `running`
4. 使用稳定 `sessionKey` 在本地执行
5. 若 agent 输出需要用户确认：
   - 将问题发到真实用户 channel/chat
   - 回写 `waiting_user`
   - 本地保存等待状态
6. 当真实用户回复后：
   - 回写 `running`
   - 用原 `sessionKey` 恢复执行
7. 执行成功：回写 `completed`
8. 执行失败：回写 `failed`

## Error handling contract

服务端建议遵循：

- `400 Bad Request`
  - 缺少字段、字段格式错误
- `401 Unauthorized`
  - token 不合法
- `404 Not Found`
  - agent 或 task 不存在
- `409 Conflict`
  - 任务状态冲突，例如已被其他 worker 认领
- `429 Too Many Requests`
  - 可选，限流
- `500 Internal Server Error`
  - 服务端内部错误

对 `claim-next`：

- 如果没有任务，不要返回 `404`
- 应返回 `200` + `{ "found": false }`

## Minimal backend checklist

后端大模型实现时，至少完成：

- `POST /api/agents/:agentId/tasks/claim-next`
- `POST /api/agents/:agentId/tasks/:taskId/status`
- `POST /api/agents/:agentId/daily-summary`
- Bearer token 认证
- 任务原子认领
- `waiting_user` 状态持久化
- 相同任务的多次 `status` 更新幂等处理

## Compatibility note

如果后端未来想同时保留 WebSocket command push 与 HTTP task runtime，两套协议可以并存：

- 旧的 `command.dispatch` WebSocket 协议仍服务于即时命令
- 新的 HTTP task runtime 协议专门服务于可认领、可恢复、可等待用户确认的任务

Kabot gateway 的 task runtime 将优先使用本文档定义的 HTTP 协议。
