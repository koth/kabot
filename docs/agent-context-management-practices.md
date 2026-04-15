# 长对话 Agent 的上下文管理工程实践

本文档总结了构建长对话 Agent 时，如何管理系统上下文、工具调用消息、以及上下文窗口膨胀问题的通用工程实践。内容剥离了具体框架与代码路径，可直接迁移到其他 LLM Agent 系统中。

---

## 1. 核心设计原则

### 1.1 以 API 端的 `usage` 为唯一可信基准
永远不要自己累加 token。大模型 API（如 Anthropic、OpenAI）会在每次响应的 `usage` 字段给出该次请求的精确输入/输出/cache token 数。客户端应将该值当作“上一次快照”，之后新增的消息只做粗略估计。

### 1.2 消息是异构的
对话历史不是简单的 `{"role": "user", "content": "..."}` 数组。需要在内存中维护一个比 API 请求更丰富的消息模型，至少包含：
- **普通用户/助手消息**
- **工具调用请求**（`tool_use` blocks）
- **工具执行结果**（`tool_result` blocks）
- **系统元消息**（compact boundary、streaming events、attachments）
- **进度消息 / 虚拟消息**（仅 UI 展示，不发给模型）

### 1.3 发送前做一次“投影”（Projection）
内存中的完整历史只供 UI 与持久化使用。真正发起 LLM 调用前，要把完整历史投影成 API 允许的格式。包括：
- 过滤掉虚拟/进度/内部系统消息
- 把附件消息重新定位到最近的 `assistant` 或 `tool_result` 旁边
- 合并零散的系统提示文本到相邻的 `tool_result` 内部（避免在 prompt 中生成异常的 Human→Human 段落）
- 强制配对：`tool_use` 必须有对应的 `tool_result`，缺一个就补一个带 `is_error` 的占位结果

---

## 2. 工具调用在消息中的表示

消息协议需要支持 **content blocks**，而不是纯字符串。

### 2.1 Assistant 发起工具调用

```json
{
  "role": "assistant",
  "content": [
    { "type": "text", "text": "我来看看文件结构" },
    { "type": "tool_use", "id": "tu_01", "name": "ReadFile", "input": { "path": "/src/main.js" } },
    { "type": "tool_use", "id": "tu_02", "name": "ReadFile", "input": { "path": "/src/util.js" } }
  ]
}
```

- **全局唯一 ID**：每个 `tool_use` 必须有一个稳定的唯一 ID。
- **并行调用**：一次 assistant 消息里可以有多个 `tool_use` block。执行器应在收到流式 block 后并行或尽早调度执行。

### 2.2 User 返回工具结果

```json
{
  "role": "user",
  "content": [
    {
      "type": "tool_result",
      "tool_use_id": "tu_01",
      "content": "export const foo = 1;",
      "is_error": false
    },
    {
      "type": "tool_result",
      "tool_use_id": "tu_02",
      "content": "File not found",
      "is_error": true
    }
  ]
}
```

- **严格 1:1 配对**：如果某个 `tool_use` 因为网络中断、用户取消或代码异常而没有结果，系统必须**自动合成**一个 `tool_result`，`is_error` 设为 `true`，并写上一句固定文案（如 `[Tool result missing due to internal error]`）。否则 API 会 400。
- **配对校验要在投影阶段做**：遍历待发送消息列表，找到尚未闭合的 `tool_use`，在当前轮次末尾补齐。

---

## 3. 上下文窗口监控

### 3.1 基准 + 估计的混合算法
不要尝试在整个消息数组上跑本地 tokenizer（太慢且不准）。推荐的算法：

```python
def estimated_token_count(messages):
    # 从后往前找最近一次带真实 usage 的 assistant 消息
    for msg in reversed(messages):
        if msg.role == 'assistant' and msg.usage is not None:
            # 找到该次 API 调用的精确 token 数（含 input + output + cache）
            baseline = msg.usage.total_tokens
            
            # 注意：如果上一次 assistant 消息被拆成了多条记录
            # （例如因为并行工具调用，每个 tool_use 都单独成了一条消息）
            # 要往前回溯到同一 response ID 的第一条拆分记录
            anchor_index = find_first_split_index(messages, msg.id)
            
            # 对 anchor 之后新增的消息做粗略估计
            added_est = rough_estimate(messages[anchor_index+1:])
            return baseline + added_est
    
    # 没有任何 usage 记录时，全部粗略估计
    return rough_estimate(messages)
```

### 3.2 粗略估计策略
对没有 usage 的新消息，简单按字符数估算即可（如 `len(text) // 4`），无需引入专用 tokenizer。主信号是最近一次 API 的精确 usage，粗略估计只覆盖“最后一轮到当前”的增量。

### 3.3 阈值设计
假设模型上下文窗口为 `WINDOW`（如 200K、1M）：

```python
RESERVED_OUTPUT = 20_000          # 为摘要/长输出预留的上限空间
EFFECTIVE_WINDOW = WINDOW - RESERVED_OUTPUT

AUTOCOMPACT_BUFFER = 13_000       # 触发自动压缩的缓冲
WARNING_BUFFER = 20_000           # UI 发出黄色警告的缓冲
BLOCKING_BUFFER = 3_000           # 硬拦截请求的缓冲

auto_compact_threshold = EFFECTIVE_WINDOW - AUTOCOMPACT_BUFFER
blocking_limit = EFFECTIVE_WINDOW - BLOCKING_BUFFER
```

状态机应暴露三个布尔值：
- `is_above_auto_compact_threshold`
- `is_above_warning_threshold`
- `is_at_blocking_limit`

---

## 4. 上下文压缩体系（多层防御）

不要把鸡蛋放在一个篮子里。当上下文膨胀时，需要**前置、被动、截断**三层防御。

### Layer 1：Snip（尾部裁剪）
在发送请求前，检查消息数组末尾是否有一段“受保护的 tail”（通常是最近 1~2 轮 assistant 消息 + 用户的最新输入）。受保护 tail 之外的所有较老历史，如果已经不可能被局部恢复，可以直接从本次 API 请求中**物理删除**。这不会丢数据，因为：
- 它们仍保留在完整内存/持久化日志中（供 UI 回滚查看）
- 只是不出现在本次 LLM prompt 里

### Layer 2：Microcompact（缓存级细粒度压缩）
如果框架支持 prompt caching，可以利用缓存编辑语义：删除已经被 cache 覆盖但内容冗余的消息（比如某些工具的大段返回结果），此时 token 释放量可以从 API 的 `cache_deleted_input_tokens` 字段精确读出。

### Layer 3：Context Collapse（折叠）
将**连续若干轮**的对话压缩成一轮摘要。例如把：

```
User: 帮我改代码
Assistant: [调用 ReadFile]
User: [File content A]
Assistant: [调用 ReadFile]
User: [File content B]
Assistant: [分析]
```

折叠成：

```
User: <collapsed>之前让用户读取了 A 和 B，Assistant 分析后认为需要重构</collapsed>
```

这部分折叠体不在主消息数组中存活，而是存放在一个“折叠日志”里，每次查询时重新投影出来。

### Layer 4：Proactive Autocompact（主动压缩）
这是最关键的兜底。在调用 API 之前，如果 `estimated_token_count > auto_compact_threshold`，先不调用主模型，而是启动一个**压缩 agent**（或走同模型的小调用）：

1. 把待压缩的历史消息发给一个专门用于摘要的 prompt（如“请总结以下对话，保留关键文件路径、决策与未完成的任务”）。
2. 限制摘要输出 token（如 20K）。
3. 得到摘要后，**替换**掉被压缩的旧消息，新的消息数组变成：
   - **Compact Boundary**：一个元数据标记（记录被压缩前的 token 数、保留段指针）
   - **Summary Message**：user message，内容是摘要文本
   - **Kept Messages**：最近 N 轮（如 2~4 轮）的原始消息，保留细节
   - **Re-injected Attachments**：压缩后需要重新声明的系统状态（可用工具列表、skills、最近读取的文件摘要、plan mode 说明等）

> **关键**：压缩不仅是删消息，还要把“系统上下文”重新注入一次，否则模型会忘记自己有什么工具、当前在什么模式。

### Layer 5：Reactive Compact（被动压缩 / 413 恢复）
如果 proactive 没拦住，API 真的返回了 `413 / prompt too long`（或 image too large 等可恢复错误），不要直接抛给用户。而是：
1. 拦截该错误，**不要**把它 yield 给 UI/上游。
2. 立即调用一次 compact（与 Layer 4 逻辑相同）。
3. 用压缩后的新消息数组**原地重试**同一请求。
4. 如果重试仍 413，再尝试二次压缩（如截断更老的消息头）；达到一定次数后放弃并返回错误。

### Last Resort：Blocking（硬拦截）
如果 reactive compact 也失败，且 `estimated_token_count >= blocking_limit`，直接返回一个固定的错误文案给用户（如“对话太长，请手动压缩或清除历史”），不再浪费 API token。

### 熔断机制
自动压缩可能反复失败（比如上下文已经大到连压缩请求本身都 413）。需要加一个**失败计数器**：连续失败 ≥ 3 次后，永久跳过 proactive autocompact，直到用户手动干预。

---

## 5. 压缩时的工程细节

### 5.1 压缩前先 Strip 多媒体
把历史中的图片、PDF 等二进制内容替换为纯文本占位符（如 `[image]`、`[document]`）。多媒体不参与摘要生成，却很容易让压缩请求先撞墙。

### 5.2 保留最近的文件状态
在压缩前，把当前已被读取的文件内容的“快照”保存下来。压缩完成后，把其中最常访问的 3~5 个文件以精简形式重新注入为附件（如只保留前 N 行 + 最近修改的关键行）。这能显著降低模型在压缩后频繁重读文件的概率。

### 5.3 保留技能与 Plan Mode 上下文
如果系统支持 skills 或 plan mode，压缩后必须重新注入：
- 当前已加载的技能列表
- 处于 plan mode 的说明
- 当前代理（agent）的上下文（如子 agent 的任务分配）

### 5.4 压缩边界与可恢复性
每次压缩要在消息流中插入一个**不可见的边界标记**，记录：
- 压缩前的 token 数
- 保留段的头尾 UUID
- 压缩触发原因（auto / manual / reactive）

这使得 `--resume` 或 UI 历史回溯时，可以正确重连被保留的原始消息与摘要之间的链条。

---

## 6. 发送给 LLM 前的最终校验清单

在把 `messages` 数组真正传给 HTTP client 之前，建议跑一遍以下固定校验：

1. **附件冒泡**：所有 attachments 必须位于最近的 `assistant` 或 `tool_result` 之后，不能孤悬在 user 输入中间。
2. **系统提示合并**：如果 user message 里同时有 `tool_result` 和以 `<system-reminder>` 开头的文本 block，把文本 block **合并进** `tool_result` 的内容数组中，不要让它们成为独立的 Human 段落。
3. **虚拟消息过滤**：所有 `is_virtual` 的消息必须被移除。
4. **工具引用清理**：如果某些工具再也不可用，把 `tool_result` 内部对它们的引用 block 也一并清理。
5. **强制配对扫描**：遍历所有消息，确保每个 `tool_use` 都有对应的 `tool_result`。
6. **Error 结果清理**：如果 `tool_result` 的 `is_error == true`，其内部 content 必须全为 text block，不能混图片。

---

## 7. 子代理（Subagent）结果如何回流主上下文

子代理是**独立的 query loop**，拥有自己隔离的 `messages` 数组。它不会在执行过程中把中间历史实时塞回主链，而是在任务结束后以受控方式把“产出”注入主上下文。

### 7.1 同步子代理
主代理阻塞等待子代理完成。子代理结束后：
1. **提取最终文本**：从子代理的消息数组中，找到最后一条包含文本的 `assistant` 消息（如果最后一条是纯 `tool_use`，则向前回溯）。
2. **封装为 `tool_result`**：把最终文本 + 子代理的 `agentId` + `usage` 统计（token 数、工具调用数、耗时）打包成一个 `tool_result` block。
3. **返回主代理**：这个 `tool_result` 与普通工具结果无异，成为主对话中一条 `user` message 的内容块。

> 即使子代理内部产生了数十轮工具调用与思考，主代理看到的**只有这一段最终摘要**。

### 7.2 异步/后台子代理
若子代理被标记为后台运行（`run_in_background` 或被强制异步）：
1. **立即返回启动回执**：主代理立刻收到一个 `tool_result`，提示“代理已在后台启动，完成后会通知你”。
2. **后台独立运行**：子代理在独立的上下文窗口中继续执行，与主代理完全隔离。
3. **完成后发通知**：子代理结束时，通过消息队列向主链注入一条 `<task-notification>` 形式的 `user` message，包含状态（completed/failed/killed）、最终摘要、`output_file` 路径等。
4. **主代理在后续轮次读取**：主代理把该通知当作新的用户输入处理。

### 7.3 Fork 子代理的特殊性
Fork 子代理为了复用父代理的 prompt cache，会继承父代理的系统提示、工具定义与消息前缀。但它的结果回流方式与普通子代理一致：跑完后提取最终文本，以 `tool_result` 或 `task-notification` 形式回到主链。

### 7.4 设计要点
- **绝不回流完整对话历史**：子代理的中间过程、错误重试、内部工具调用等不得进入主上下文。
- **保留可续接标识**：同步结果中附带 `agentId`，主代理后续可用 `SendMessage` 等工具继续与该子代理交互。
- **Worktree 隔离信息**：若子代理在独立 worktree 中执行，结果中应包含 `worktreePath`，以便主代理在需要时定位其文件变更。

---

## 8. 一句话总结

> 用 **API usage 做精确基准 + 增量粗略估计** 来测量窗口；用 **Snip → Microcompact → Collapse → Proactive Autocompact → Reactive Compact** 的五层防御体系逐步消化膨胀；所有发向 LLM 的消息必须在投影阶段经过 **附件冒泡、系统提示合并、tool_use/tool_result 强制配对** 三道关卡。
