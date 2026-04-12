# Relay Server 接口实现指南：Agent 任务提交

本文档面向 Relay Server（后端）开发者，说明需要实现的 HTTP 接口，以便 **Kabot Agent** 端能够成功提交任务到项目中。

---

## 1. 接口目标

Agent 侧通过 `PlanWorkTool` 将大模型拆解出的任务列表提交到 Relay Server。这些任务是**项目级（Project-scoped）**的，创建时**不绑定任何特定 Agent**（unassigned），后续再由 Relay Server 的调度逻辑分配给合适的 Agent 执行。

---

## 2. 基础信息

| 项目 | 说明 |
|------|------|
| **端点** | `POST /api/projects/{projectId}/tasks` |
| **Content-Type** | `application/json` |
| **认证方式** | `Authorization: Bearer <token>` |
| **调用方** | Kabot Agent (`RelayManager::SubmitProjectTask`) |

Agent 使用的 `token` 与 `ClaimNextTask`、`UpdateTaskStatus` 等接口共用同一套 `RelayManagedAgentConfig` 中的 `token`。

---

## 3. 请求体（Request Body）

### 3.1 字段定义

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `title` | `string` | ✅ | 任务标题 |
| `instruction` | `string` | ✅ | 任务详细指令 |
| `priority` | `string` |  | 优先级，例如 `"high"`、`"normal"`、`"low"` |
| `projectId` | `string` |  | 项目唯一标识（URL 中已有 `{projectId}`，Body 中也可能重复携带） |
| `mergeRequest` | `string` |  | 关联的 Merge Request / PR 编号 |
| `interaction` | `object` |  | 交互上下文，包含触发该任务的聊天渠道信息 |
| `interaction.channel` | `string` |  | 渠道类型，如 `"telegram"`、`"lark"` |
| `interaction.channelInstance` | `string` |  | 渠道实例名，如 `"telegram_ops"` |
| `interaction.chatId` | `string` |  | 聊天会话 ID |
| `interaction.replyTo` | `string` |  | 需要回复的消息 ID（通常为空） |
| `dependsOn` | `string[]` |  | 依赖的任务标识列表。**当前 Agent 版本通常留空。** |
| `metadata` | `object<string,string>` |  | 附加元数据。当前版本会在 `metadata.depends_on_titles` 中存放逗号分隔的依赖任务标题列表，例如 `"Setup project,Write tests"` |

### 3.2 示例请求体

```json
{
  "title": "Write tests",
  "instruction": "Add unit tests for the auth module",
  "priority": "normal",
  "projectId": "proj-42",
  "mergeRequest": "mr-7",
  "interaction": {
    "channel": "telegram",
    "channelInstance": "telegram_ops",
    "chatId": "chat-99",
    "replyTo": ""
  },
  "dependsOn": [],
  "metadata": {
    "depends_on_titles": "Setup project"
  }
}
```

---

## 4. 响应格式（Response）

### 4.1 成功响应

当任务创建成功时，返回 **HTTP 2xx**，并在 JSON 响应体中包含服务器分配的任务 ID：

```json
{
  "taskId": "task-abc-123"
}
```

**Agent 侧解析逻辑**：Agent 会读取 `response.body` 中 JSON 对象的 `taskId` 字段（string 类型），并将其返回给用户作为提交确认信息。如果 `taskId` 缺失，Agent 仍认为 HTTP 层面提交成功，只是不会显示具体 ID。

### 4.2 失败响应

当创建失败时，返回 **非 2xx** HTTP 状态码。Agent 会读取 `response.body` 原文（若为空则读取 HTTP reason phrase）作为错误消息返回给用户。

```json
{
  "error": "Project not found: proj-42"
}
```

或者简单的纯文本错误信息亦可：

```text
Invalid priority value: urgent
```

---

## 5. 后端实现示例（Node.js / Express 风格伪代码）

```javascript
const express = require('express');
const router = express.Router();

// 认证中间件（示例）
function authenticateAgent(req, res, next) {
  const auth = req.headers['authorization'] || '';
  const token = auth.replace(/^Bearer\s+/i, '');
  if (!isValidAgentToken(token)) {
    return res.status(401).json({ error: 'invalid token' });
  }
  next();
}

router.post('/api/projects/:projectId/tasks', authenticateAgent, (req, res) => {
  const { projectId } = req.params;
  const {
    title,
    instruction,
    priority,
    mergeRequest,
    interaction,
    dependsOn,
    metadata
  } = req.body;

  if (!title || !instruction) {
    return res.status(400).json({ error: 'title and instruction are required' });
  }

  // 1. 创建任务记录
  const task = createTask({
    projectId,
    title,
    instruction,
    priority: priority || 'normal',
    mergeRequest: mergeRequest || null,
    interaction: interaction || {},
    // 任务创建时状态建议为 waiting / pending，等待后续调度
    status: 'waiting',
    assignedAgentId: null,
    createdAt: new Date().toISOString(),
    // 依赖处理
    dependsOn: dependsOn || [],
    metadata: metadata || {}
  });

  // 2. 如果 metadata 中有 depends_on_titles，可尝试在本项目下按 title 查找已有任务并建立依赖关联
  if (metadata && metadata.depends_on_titles) {
    const titles = metadata.depends_on_titles.split(',').map(s => s.trim());
    for (const depTitle of titles) {
      const depTask = findTaskByProjectAndTitle(projectId, depTitle);
      if (depTask) {
        linkTaskDependency(task.id, depTask.id);
      }
    }
  }

  // 3. 返回成功响应
  return res.status(201).json({ taskId: task.id });
});
```

---

## 6. 实现注意事项

### 6.1 任务归属

此接口创建的任务**不绑定到任何 Agent**。建议数据库中 `assigned_agent_id` 初始为 `NULL` 或空字符串，由 Relay Server 的独立调度器后续根据任务负载、Agent 能力标签等进行分配。

### 6.2 依赖处理策略

当前 Agent 版本采用**顺序逐个提交**任务的方式。因此：

- 当后提交的任务依赖先提交的任务时，先提交的任务已经生成 `taskId`。
- Agent 当前并未在 `dependsOn` 数组中填充 `taskId`，而是把依赖任务标题放到了 `metadata.depends_on_titles` 里。
- **Relay Server 后端推荐实现**：接收到任务创建请求后，检查 `metadata.depends_on_titles`，在当前 `projectId` 下搜索已存在的同 title 任务，并将其 `taskId` 写入本条任务的依赖列表中。如果找不到对应任务，可暂时保留 title 字符串，或标记为依赖缺失。

### 6.3 幂等性与重复提交

Agent 的 `PlanWorkTool` 在用户多次调用时可能重复提交相同内容。Relay Server 可根据业务需要考虑幂等性设计（例如基于 `title + projectId + instruction` 的哈希去重，或支持客户端传入 `idempotency-key`）。目前 Agent 端不携带幂等键。

### 6.4 连接复用

Agent 侧会尽量复用同一 TCP/TLS 连接发送多个任务创建请求（HTTP Keep-Alive）。Relay Server 无需特殊处理，只需支持常规 HTTP 持久连接即可。如果连接 idle 超时断开，Agent 会自动重建连接并重试。

### 6.5 字段命名兼容性

Agent 发送的 JSON 字段采用 camelCase：`projectId`、`mergeRequest`、`channelInstance`、`chatId`、`replyTo`、`dependsOn`。请确保后端解析器与此保持一致。

---

## 7. 快速核对清单

- [ ] 已实现 `POST /api/projects/{projectId}/tasks`
- [ ] 已校验 `Authorization: Bearer <token>`
- [ ] 已校验 `title` 和 `instruction` 必填
- [ ] 成功时返回 HTTP 2xx + JSON `{ "taskId": "..." }`
- [ ] 失败时返回非 2xx + 可读错误信息
- [ ] 已处理 `metadata.depends_on_titles` 中的依赖任务关联（推荐）
- [ ] 新建任务默认状态为未分配（unassigned）

完成以上接口后，Agent 侧的 `plan_work` 工具即可正常将大模型生成的任务计划提交到你的 Relay Server 中。
