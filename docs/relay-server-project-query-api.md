# Relay Server 接口实现指南：项目信息查询

本文档面向 Relay Server（后端）开发者，说明需要实现的 HTTP 接口，以便 **Kabot Agent** 端能够在拆解任务前获取项目简介，从而生成更准确的任务计划。

---

## 1. 接口目标

Agent 侧的 `PlanWorkTool` 在检测到用户传入了 `project_id` 时，会优先调用 `RelayManager::QueryProject` 向 Relay Server 查询该项目的描述、名称和元数据，并将这些信息注入 LLM 的提示词中。如果查询失败，Agent 会 gracefully 降级，仅依据用户指令进行任务拆解。

---

## 2. 基础信息

| 项目 | 说明 |
|------|------|
| **端点** | `GET /api/projects/{projectId}` |
| **Content-Type** | `application/json` |
| **认证方式** | `Authorization: Bearer <token>` |
| **调用方** | Kabot Agent (`RelayManager::QueryProject`) |

Agent 使用的 `token` 与 `ClaimNextTask`、`UpdateTaskStatus`、`SubmitProjectTask` 等接口共用同一套 `RelayManagedAgentConfig` 中的 `token`。

---

## 3. 路径参数

| 参数名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `projectId` | `string` | ✅ | URL 路径中的项目唯一标识。例如：`proj-42` |

---

## 4. 响应格式（Response）

### 4.1 成功响应

当项目存在时，返回 **HTTP 2xx**，并在 JSON 响应体中包含以下字段：

| 字段名 | 类型 | 必填 | 说明 |
|--------|------|------|------|
| `projectId` | `string` | 推荐 | 项目的唯一标识。Agent 会用它覆盖请求中的 `projectId` |
| `name` | `string` |  | 项目名称 |
| `description` | `string` | 推荐 | **项目简介/描述**。这是 Agent 注入 LLM 提示词的核心字段 |
| `metadata` | `object<string,string>` |  | 附加元数据键值对。非字符串类型的值会被 Agent 忽略 |

#### 成功响应示例

```json
{
  "projectId": "proj-42",
  "name": "Kabot 客户端重构",
  "description": "将现有 C++ 客户端拆分为 core / cli / plugin 三层架构，统一使用 Boost.Asio 作为网络层，并提供清晰的 C API 以便后续绑定。",
  "metadata": {
    "techStack": "C++20, Boost, CMake",
    "priority": "high",
    "owner": "backend-team"
  }
}
```

**Agent 侧解析逻辑**：
- Agent 会读取 `response.body` 中的 JSON 对象。
- 如果 `description` 存在且非空，则会将其附加到 LLM 的 system prompt 中。
- `metadata` 中的内容当前仅做记录，不直接参与提示词生成（可未来扩展）。

### 4.2 失败响应

当项目不存在、认证失败或发生内部错误时，返回 **非 2xx** HTTP 状态码。Agent 会读取 `response.body` 原文（若为空则读取 HTTP reason phrase）作为错误消息，并降级为无项目上下文的任务拆解。

#### 失败响应示例

```json
{
  "error": "Project not found: proj-99"
}
```

或简单的纯文本：

```text
Unauthorized
```

常见状态码建议：
- `401 Unauthorized` — token 无效或缺失
- `403 Forbidden` — token 无权访问该项目
- `404 Not Found` — 项目 ID 不存在
- `500 Internal Server Error` — 服务器内部错误

---

## 5. 后端实现示例

### Node.js / Express 风格

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

router.get('/api/projects/:projectId', authenticateAgent, (req, res) => {
  const { projectId } = req.params;

  const project = findProjectById(projectId);
  if (!project) {
    return res.status(404).json({ error: 'Project not found: ' + projectId });
  }

  return res.status(200).json({
    projectId: project.id,
    name: project.name,
    description: project.description,
    metadata: project.metadata || {}
  });
});
```

### Python / FastAPI 风格

```python
from fastapi import APIRouter, Depends, HTTPException, Header

router = APIRouter()

def verify_token(authorization: str = Header(...)):
    token = authorization.removeprefix("Bearer ")
    if not is_valid_agent_token(token):
        raise HTTPException(status_code=401, detail="invalid token")

@router.get("/api/projects/{project_id}")
async def get_project(project_id: str, _=Depends(verify_token)):
    project = find_project_by_id(project_id)
    if not project:
        raise HTTPException(status_code=404, detail=f"Project not found: {project_id}")
    return {
        "projectId": project.id,
        "name": project.name,
        "description": project.description,
        "metadata": project.metadata or {}
    }
```

---

## 6. 实现注意事项

### 6.1 字段命名兼容性

Agent 发送请求时不携带请求体，只解析响应。响应字段采用 camelCase：
- `projectId`
- `mergeRequest`（本项目暂时不涉及）
- `metadata`

请确保后端返回的 JSON 字段名与此保持一致。

### 6.2 描述内容建议

`description` 是 Agent 注入 LLM prompt 的核心内容。建议包含：
- 项目目标和范围（1-3 句话）
- 关键技术栈 / 约束
- 任何 Agent 在拆解任务时应当注意的特定要求

例如：
> "将现有 C++ 客户端拆分为 core / cli / plugin 三层架构，统一使用 Boost.Asio 作为网络层，并提供清晰的 C API 以便后续绑定。要求所有外部依赖通过 vcpkg 管理，保持 Windows / macOS / Linux 三平台兼容。"

### 6.3 连接复用

Agent 会尽量复用同一 TCP/TLS 连接发送多个请求（HTTP Keep-Alive）。Relay Server 无需特殊处理，只需支持常规 HTTP 持久连接即可。

### 6.4 失败降级

Agent 在调用本接口失败时**不会阻塞用户请求**，而是：
1. 记录一条 warning 日志
2. 继续仅基于用户指令进行任务拆解
3. 在返回结果中提示用户 "could not fetch project description from relay server"

因此，即使 Relay Server 暂时未实现该接口，Agent 的其他功能也能正常运行。

---

## 7. 快速核对清单

- [ ] 已实现 `GET /api/projects/{projectId}`
- [ ] 已校验 `Authorization: Bearer <token>`
- [ ] 成功时返回 HTTP 2xx + JSON（至少包含 `description`）
- [ ] 失败时返回非 2xx + 可读错误信息
- [ ] 字段命名使用 camelCase（`projectId`、`metadata`）

完成以上接口后，Agent 侧的 `plan_work` 工具就能够在拆解任务前拉取项目上下文，从而生成更贴合项目需求的任务计划。
