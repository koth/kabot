# Subagent 机制实现说明

本文档面向需要"阅读后复刻 subagent 机制"的 LLM 或工程师，目标不是解释单个函数，而是提炼一套可以迁移到其他代码库中的设计方案。

---

# 1. 目标与总体思路

这套系统里的 subagent 不是独立微服务，也不是简单地在主 agent 内写几个辅助函数，而是一个完整的"子代理执行框架"。

它的核心目标是：

- 让主 agent 可以把任务委派给不同角色的子 agent
- 让子 agent 复用同一套模型循环内核
- 让子 agent 拥有独立的上下文、权限、工具和生命周期
- 让子 agent 可以同步运行，也可以作为后台任务异步运行
- 让子 agent 的执行过程可记录、可恢复、可追踪、可清理

## 1.1 文档目标

这份文档的目标不是帮助读者“理解大概思想”，而是帮助另一个 LLM 在**看不到当前仓库源码**的前提下，仍然能实现一套功能完整的 subagent 系统。

因此本文会同时提供：

- 架构原则
- 模块拆分
- 核心数据结构
- 关键接口契约
- 生命周期状态机
- 失败处理策略
- 最小伪代码骨架

## 1.2 文档非目标

本文不会强制绑定以下具体实现：

- 某个特定模型 SDK
- 某个特定前端 UI 框架
- 某个特定任务队列
- 某个特定持久化存储
- 某个特定工具协议

也就是说，你可以把本文中的抽象映射到：

- Node.js
- Python
- 单进程 agent 系统
- 多进程 agent 系统
- 有 UI 的产品
- 纯 API 后端

只要保留核心不变量，机制就可以成立。

## 1.3 必须满足的系统能力

如果要宣称“实现了 subagent 机制”，至少要满足以下能力：

- 能定义多个 agent 角色
- 能由主 agent 派生子 agent
- 子 agent 有独立上下文和身份
- 子 agent 能使用受控工具集
- 子 agent 能同步或异步执行
- 子 agent 的执行可追踪、可取消、可清理
- 至少异步路径支持恢复或重建运行上下文

可以把整体架构概括为：

```text
AgentDefinition
  -> AgentTool 调度
  -> runAgent 执行
  -> createSubagentContext 构造隔离上下文
  -> query() 驱动模型循环
  -> transcript/task system 管理生命周期
```

也就是说，subagent 机制并不是一个点功能，而是下面几个模块的协作结果：

- agent 定义系统
- agent 启动入口
- agent 运行器
- 子上下文隔离层
- 后台任务系统
- transcript 持久化系统
- resume 恢复系统
- 并发 attribution 系统

## 1.4 最高层不变量

无论你用什么语言实现，下面这些不变量最好保持不变：

- **[统一执行内核]** 主 agent 与 subagent 最终走同一套推理/工具循环
- **[独立身份]** 每个 subagent 必须有唯一 `agentId`
- **[上下文隔离]** child 不得直接共享 parent 的全部可变状态
- **[工具受控]** subagent 的工具集必须经过过滤和派生
- **[生命周期闭环]** spawn、运行、完成、失败、中断、清理必须可观测
- **[可追溯]** 子代理的输入、输出、metadata 必须可持久化
- **[并发归属]** 并发 subagent 必须能区分日志、指标、trace 的归属

---

# 2. 模块总览

如果要从零实现这套机制，优先建立这些模块：

## 2.1 入口与调度模块

这是 subagent 的主入口。

职责：

- 解析 AgentTool 输入
- 选择目标 agent
- 决定是否走 fork 路径
- 决定同步/异步运行
- 决定是否 worktree 隔离
- 组装 `runAgent()` 参数
- 把 agent 接入 foreground/background 生命周期

## 2.2 运行核心模块

这是 subagent 真正的执行核心。

职责：

- 构造 agent 的 prompt / context / tools / permissions
- 初始化 agent 专属 MCP server
- 创建 subagent 的 `ToolUseContext`
- 调用统一的 `query()` 模型循环
- 记录 transcript
- 做清理和收尾

## 2.3 子上下文隔离模块

这里的 `createSubagentContext()` 是整个设计最关键的部分之一。

职责：

- 决定哪些状态从父 agent 继承
- 决定哪些状态必须克隆或隔离
- 决定权限弹窗、响应统计、任务状态是否共享

## 2.4 agent 定义模块

职责：

- 定义 `AgentDefinition`
- 加载 built-in / plugin / user / project agent
- 统一 agent 的配置模型

## 2.5 内置 agent 注册模块

职责：

- 返回内置 agents 列表
- 控制哪些 built-in agents 默认启用

## 2.6 fork 子代理模块

职责：

- 定义隐式 fork agent
- 构造 fork child 的上下文消息
- 避免递归 fork
- 优化 prompt cache 命中率

## 2.7 恢复模块

职责：

- 根据持久化 metadata 和 transcript 恢复后台 agent

## 2.8 并发上下文归属模块

职责：

- 用 `AsyncLocalStorage` 维护 agent 身份
- 支持并发 subagent 的 attribution / telemetry 隔离

## 2.9 工具过滤与任务辅助模块

职责：

- 解析 agent 能用哪些工具
- 管理异步 agent 生命周期的一部分辅助逻辑
- 支持 summarization / progress / completion / failure

---

# 3. 核心接口契约

如果要让别的 LLM 更容易直接生成代码，必须把关键抽象从“概念”提升到“接口契约”。

下面给出建议的数据结构。字段可以增减，但语义最好保留。

## 3.1 AgentDefinition

```ts
type AgentDefinition = {
  agentType: string
  whenToUse: string
  getSystemPrompt: (params: { runtime: RuntimeView }) => string
  tools?: string[]
  disallowedTools?: string[]
  model?: string
  permissionMode?: 'default' | 'acceptEdits' | 'plan' | 'bubble' | 'bypassPermissions'
  maxTurns?: number
  background?: boolean
  memory?: 'none' | 'user' | 'project' | 'local'
  isolation?: 'none' | 'worktree' | 'remote'
  mcpServers?: AgentMcpServerSpec[]
  skills?: string[]
  initialPrompt?: string
  metadata?: Record<string, unknown>
}
```

## 3.2 AgentSpawnInput

```ts
type AgentSpawnInput = {
  prompt: string
  subagentType?: string
  description?: string
  model?: string
  runInBackground?: boolean
  name?: string
  teamName?: string
  mode?: 'spawn' | 'plan'
  isolation?: 'none' | 'worktree' | 'remote'
  cwd?: string
}
```

## 3.3 ToolUseContext

```ts
type ToolUseContext = {
  agentId: string
  messages: Message[]
  readFileState: FileReadCache
  abortController: AbortController
  options: RuntimeOptions
  getAppState: () => AppState
  setAppState?: (updater: (prev: AppState) => AppState) => void
  setAppStateForTasks?: (updater: (prev: AppState) => AppState) => void
  setResponseLength?: (n: number) => void
  pushApiMetricsEntry?: (ms: number) => void
  preserveToolUseResults?: boolean
}
```

## 3.4 RunAgentParams

```ts
type RunAgentParams = {
  agentDefinition: AgentDefinition
  promptMessages: Message[]
  toolUseContext: ToolUseContext
  availableTools: Tool[]
  canUseTool: CanUseToolFn
  isAsync: boolean
  querySource: string
  model?: string
  maxTurns?: number
  allowedTools?: string[]
  forkContextMessages?: Message[]
  useExactTools?: boolean
  description?: string
  worktreePath?: string
  onQueryProgress?: () => void
}
```

## 3.5 异步任务记录结构

```ts
type AgentTaskRecord = {
  taskId: string
  agentId: string
  description?: string
  status: 'queued' | 'running' | 'backgrounded' | 'completed' | 'failed' | 'aborted'
  startedAt: number
  finishedAt?: number
  outputFile?: string
  progress?: {
    message?: string
    lastToolName?: string
    tokens?: number
    turns?: number
  }
  error?: {
    code: string
    message: string
    retryable: boolean
  }
}
```

## 3.6 Transcript metadata

```ts
type AgentTranscriptMetadata = {
  agentId: string
  agentType: string
  description?: string
  worktreePath?: string
  parentAgentId?: string
  parentSessionId?: string
  invocationKind: 'spawn' | 'resume'
  createdAt: number
}
```

---

# 4. 系统状态机

要让 LLM 真正复现，不能只写“会启动和清理”，而必须明确状态迁移。

## 4.1 agent 生命周期状态

建议最少有这些状态：

```text
idle
  -> spawning
  -> running_foreground
  -> backgrounded
  -> completed
  -> failed
  -> aborted
  -> resumed_running
```

## 4.2 典型状态迁移

```text
idle -> spawning
spawning -> running_foreground
spawning -> backgrounded
running_foreground -> completed
running_foreground -> failed
running_foreground -> aborted
running_foreground -> backgrounded
backgrounded -> completed
backgrounded -> failed
backgrounded -> aborted
backgrounded -> resumed_running
resumed_running -> completed
resumed_running -> failed
resumed_running -> aborted
```

## 4.3 状态迁移规则

- **[spawn 成功后必须有 agentId]**
- **[进入 running/backgrounded 前必须已注册 task 或前台句柄]**
- **[completed/failed/aborted 为终态]**
- **[resume 只能从可恢复状态进入]**
- **[任何终态都必须触发 cleanup]**

---

# 5. 顶层时序

下面给一份不依赖具体语言的顶层时序。实现时最好保持这个交互顺序。

## 5.1 普通 spawn 时序

```text
Main Agent
  -> Spawn Entry
  -> Resolve AgentDefinition
  -> Build Prompt Messages
  -> Build Child Context
  -> Resolve Tools / Permissions
  -> RunAgent
  -> Query Loop
  -> Persist Transcript
  -> Emit Progress / Final Output
  -> Cleanup
```

## 5.2 异步 spawn 时序

```text
Main Agent
  -> Spawn Entry
  -> Register Agent Task
  -> Persist Initial Metadata
  -> Launch Background Lifecycle
  -> RunAgent Stream Consumption
  -> Update Task Progress
  -> Finalize Task Status
  -> Persist Final Output
  -> Notify Completion / Failure
  -> Cleanup
```

## 5.3 resume 时序

```text
Resume Request
  -> Load Transcript Metadata
  -> Reconstruct Runtime Params
  -> Restore Context / cwd / isolation
  -> Re-attach Task Lifecycle
  -> Continue RunAgent
  -> Persist New Messages
  -> Finalize / Cleanup
```

---

 # 6. 核心设计思想
 
 要让别的 LLM 复刻这套机制，最重要的是理解它背后的设计原则，而不是死记实现细节。
 
 ## 6.1 主 agent 与 subagent 共享同一模型内核
 
 这套系统没有为 subagent 单独设计一套推理引擎。

它的实现方式是：

- 主 agent 和 subagent 最终都调用同一个 `query()`
- 区别只在于传给 `query()` 的上下文不同

 这意味着：
 
 - 主 agent 只是一个特殊的“顶层 agent”
 - subagent 是同内核、不同配置的运行实例
 
 这个设计非常重要，因为它让系统避免了两套执行引擎并存带来的复杂性。
 
 ## 6.2 AgentDefinition 与 Runner 解耦
 
 agent 的人格、模型、工具、权限、hooks、MCP 配置，并不写死在执行器中，而是由 `AgentDefinition` 提供。

也就是说：

- `AgentDefinition` 负责描述 agent 是什么
- `runAgent()` 负责描述 agent 怎么跑

 这使得系统可以非常自然地支持：
 
 - 内置 agent
 - 用户自定义 agent
 - 插件 agent
 - 项目级 agent
 
 ## 6.3 子上下文必须“隔离优先，按需共享”
 
 如果 subagent 直接共享父 agent 的全部运行状态，会出现很多问题：

- 文件缓存互相污染
- 权限判断相互影响
- 后台 agent 改坏主状态
- 并发下 attribution 错乱
- 子 agent 残留状态无法清理

 所以这里采用的是：
 
 - 默认隔离 mutable state
 - 仅在明确需要时共享某些 callback 或句柄
 
 这是 `createSubagentContext()` 的核心哲学。
 
 ## 6.4 生命周期必须闭环
 
 一个可用的 subagent 框架不能只会“启动”，还必须支持：

- 启动
- 前台执行
- 切后台执行
- 进度更新
- transcript 记录
- 完成通知
- 失败处理
- 用户中断
- resume 恢复
- 清理资源

本仓库的实现已经形成闭环，所以它不是 demo，而是可以长期跑的工程系统。

## 6.5 子代理本质上是任务系统中的一类 worker

在异步路径下，subagent 会被注册成一个后台任务。

所以设计上最好把 subagent 理解为：

- 一种由 LLM 驱动的 worker
- 有自己的 task id / agent id / output file / notification / progress

这样比把它理解成“单次函数调用”更接近真实实现。

---

# 7. AgentDefinition：subagent 的定义模型

`AgentDefinition` 是统一的数据模型，用来描述一个 agent。

关键字段包括：

- `agentType`
- `whenToUse`
- `tools`
- `disallowedTools`
- `skills`
- `mcpServers`
- `hooks`
- `model`
- `effort`
- `permissionMode`
- `maxTurns`
- `background`
- `initialPrompt`
- `memory`
- `isolation`
- `omitClaudeMd`

这说明 agent 不是只由 prompt 决定，而是由多维配置共同决定。

## 7.1 三类 agent

### Built-in agent

特点：

- 系统内置
- `getSystemPrompt()` 通常是动态生成
- 可带 callback

### Custom agent

特点：

- 来自用户/项目/策略配置
- prompt 通常来自 markdown/frontmatter

### Plugin agent

特点：

- 来自插件
- 可以携带 plugin 元数据

## 7.2 为什么这层很关键

如果你要让别的 LLM 复刻这套机制，必须先建立“agent 定义层”，否则后续：

- 工具权限控制
- 动态模型选择
- 生命周期策略
- UI 展示
- resume 路由

都会很难做统一。

---

# 8. 启动入口：AgentTool 怎么调度 subagent

`AgentTool` 是主 agent 发起委派的总入口。

## 8.1 输入参数的语义

从实现上看，典型输入包括：

- `prompt`
- `subagent_type`
- `description`
- `model`
- `run_in_background`
- `name`
- `team_name`
- `mode`
- `isolation`
- `cwd`

其中最关键的是：

- `prompt`：交给 subagent 的任务描述
- `subagent_type`：使用哪个 agent 角色
- `run_in_background`：是否后台跑
- `isolation`：是否进入隔离 worktree

## 8.2 AgentTool 的主要步骤

当主 agent 调用它时，大致流程是：

```text
1. 读取当前 app state
2. 检查权限和 team/swarm 约束
3. 解析目标 agent
4. 构建 prompt messages
5. 决定是否 fork
6. 决定是否异步运行
7. 构造 worker tool pool
8. 如有需要创建 worktree
9. 调用 runAgent()
10. 将其接入 sync 或 async 生命周期
```

## 8.3 普通路径与 fork 路径

### 普通路径

普通 subagent 的输入消息很简单：

- 直接把用户传入的 `prompt` 包装成一条 user message

### fork 路径

fork 模式下，子 agent 不是从“一个全新的问题”开始，而是从父 agent 的当前上下文分叉。

这时会：

- 保留父 assistant message
- 提取其中所有 `tool_use`
- 人工构造统一 placeholder 的 `tool_result`
- 追加新的 child directive

这部分通常由一个专门的 fork message builder 来实现。

---

# 9. fork subagent 的设计思想

这是这套系统里非常有特色的一部分。

## 9.1 fork 的触发方式

当实验开关开启时：

- 可以不传 `subagent_type`
- 系统会走一个隐式 fork agent

这个特殊 agent 用 `FORK_AGENT` 表示。

它的特点：

- `tools: ['*']`
- `model: 'inherit'`
- `permissionMode: 'bubble'`
- `maxTurns: 200`

## 9.2 为什么要伪造 tool_result

因为父 assistant message 里可能已经包含多个 `tool_use`。

如果 child 直接接着这些消息跑：

- API 侧可能认为这些 tool call 还没闭合
- 形成非法消息序列

所以实现中会为每个 `tool_use` 生成一个统一 placeholder 的 `tool_result`。

这同时还有一个额外收益：

- 多个 fork child 可以共享几乎相同的 prompt 前缀
- 从而提高 prompt cache 命中率

## 9.3 fork 不是普通 spawn

普通 spawn 更像：

- 启一个新 worker
- 给它一个任务描述

fork 更像：

- 复制当前 agent 的思维上下文
- 在该上下文末尾切一个分支继续做事

如果别的 LLM 要复刻 subagent 机制，fork 功能可以后做，但如果要做，建议单独设计，而不要复用普通 spawn 的消息构造逻辑。

---

# 10. runAgent：subagent 的统一运行器

这个函数是整个系统的核心。

函数形式是：

- `AsyncGenerator<Message, void>`

这意味着它不是一次性返回结果，而是一边运行一边流式产出消息。

## 10.1 runAgent 的职责拆解

`runAgent()` 主要负责：

- 计算 agent model
- 分配 `agentId`
- 组装初始消息
- 裁剪/构造 userContext 和 systemContext
- 推导 agent 的权限模式
- 解析可用工具
- 初始化 agent 专属 MCP server
- 构造子 `ToolUseContext`
- 调用 `query()`
- 转发流式消息
- 记录 transcript
- 在 finally 中清理资源

## 10.2 为什么它设计成 AsyncGenerator

因为 agent 运行过程中会持续产生：

- progress
- assistant message
- attachment
- stream_event

如果只返回最终结果：

- UI 无法实时展示
- 后台任务无法中途更新进度
- 前台 agent 无法被 background

所以一个成熟的 subagent runner 应该是流式的。

---

# 11. 子上下文：createSubagentContext 的实现思想

这是移植时最值得保留的部分。

## 11.1 为什么需要单独的子上下文

subagent 需要拥有：

- 自己的消息链
- 自己的文件读取缓存
- 自己的 agentId
- 自己的 abortController
- 自己的 query depth
- 自己的权限视图

但它又不应该完全脱离父上下文，因为有些东西仍然需要继承：

- 当前工具总表
- 部分 app state 读能力
- 某些统计回调
- 某些 attribution 信息

所以最好的做法不是“共享父 context”或“全部重建”，而是“基于父 context 派生一个 child context”。

## 11.2 默认隔离的内容

`createSubagentContext()` 默认会隔离或克隆：

- `readFileState`
- `nestedMemoryAttachmentTriggers`
- `loadedNestedMemoryPaths`
- `dynamicSkillDirTriggers`
- `discoveredSkillNames`
- `contentReplacementState`
- `queryTracking`

这样可以避免子代理污染父状态。

## 11.3 可选共享的内容

调用方可以显式共享：

- `setAppState`
- `setResponseLength`
- `abortController`

例如：

- 同步 agent 更倾向共享一部分 callback
- 异步 agent 更倾向隔离 mutation 能力

## 11.4 权限弹窗策略

对子代理来说，默认常见策略是：

- 避免直接弹权限 UI
- 将权限决策收敛到父层或后台策略中

因此子上下文往往会把：

- `shouldAvoidPermissionPrompts = true`

作为默认值之一。

这个设计非常适合后台 agent。

---

# 12. 工具系统：subagent 为什么不能直接继承父工具

## 12.1 工具能力需要按 agent 类型裁剪

不同 subagent 不能拿到完全一样的工具集合。

原因包括：

- 安全性
- 成本控制
- 角色边界
- 异步 agent 的限制
- plugin/tool surface 的管理

因此系统会在运行前做一次 `resolveAgentTools()` / `filterToolsForAgent()`。

## 12.2 过滤逻辑的真实特征

实现里能看到几类规则：

- 某些工具对所有 agent 禁用
- 某些工具只对自定义 agent 禁用
- 异步 agent 只能使用白名单工具
- MCP 工具通常被保留
- 某些 teammate 模式允许更特殊的工具集

## 12.3 迁移建议

如果你要让另一个 LLM 复刻这套机制，不要让 subagent 直接共享父工具数组，应该：

- 先拿到一个基础工具池
- 再按 agent 定义和运行模式过滤
- 最后生成 agent 专属工具集

## 12.4 工具过滤决策顺序

如果要让实现稳定，建议把工具过滤写成一个固定顺序的纯函数，而不是分散的条件分支。

推荐顺序：

```text
input: baseToolPool, agentDefinition, runtimeMode, permissionContext

1. 先移除全局禁用工具
2. 再移除 agentDefinition.disallowedTools
3. 若 agentDefinition.tools 存在，则只保留显式允许集合
4. 若 runtimeMode=async，则再套用 async 白名单
5. 保留必须始终可用的系统工具
6. 保留被系统认定为可附加的外部工具
7. 最终再做一次基于 permissionContext 的运行时裁剪
output: resolvedToolPool
```

## 12.5 建议保留的过滤不变量

- **[先减后加]** 优先先做删除，再处理少量保留例外
- **[纯函数]** 输入相同，输出工具集必须相同
- **[可解释]** 每个工具为什么被保留/移除应可追踪
- **[异步更严格]** async agent 工具集通常应小于等于 sync agent

---

# 13. 权限系统：subagent 应该看到怎样的权限状态

subagent 并不是直接继承父 `AppState.toolPermissionContext`，而是基于它派生出自己的权限视图。

这里的设计要点包括：

- agent 可以声明自己的 `permissionMode`
- 如果父级是某些强权限模式，子级不要随意降权或改写
- 异步 agent 一般应该避免弹 UI
- 可通过 `allowedTools` 给子级单独设定会话级允许规则

这说明权限在架构里是“运行时派生”的，而不是静态绑定在 agent 上。

如果要复刻，推荐采用：

```text
effectivePermissions = derive(parentPermissions, agentDefinition, runtimeMode)
```

而不是：

```text
effectivePermissions = parentPermissions
```

## 13.1 权限派生算法

权限不要直接继承，而要派生。推荐顺序如下：

```text
input: parentPermissionContext, agentDefinition, runtimeMode, allowedTools

1. 读取 parentPermissionContext
2. 若 agentDefinition 指定 permissionMode，则尝试覆盖默认模式
3. 若 parent 处于不可降级的高优先级模式，则禁止子级回退
4. 若 runtimeMode=async 且不能弹 UI，则设置 shouldAvoidPermissionPrompts=true
5. 若传入 allowedTools，则将其作为 session-scoped allow rules
6. 输出 effectivePermissionContext
```

## 13.2 权限实现建议

- **[父级兜底]** 父级显式禁止的行为，子级不应轻易绕过
- **[异步去交互]** 后台 agent 默认不要依赖人工弹窗完成权限授权
- **[最小授权]** 子 agent 只拿完成任务所必需的最小权限
- **[可观测]** 权限拒绝必须能被 transcript/task progress 看见

---

# 14. MCP 扩展：为什么 agent 需要自己的外接能力

实现中支持 agent 在 frontmatter 或定义里声明自己的 `mcpServers`。

运行时流程是：

- 从 agent definition 中读取 `mcpServers`
- 连接这些 server
- 拉取 server 提供的 tools
- 与 agent 当前工具集合并
- 在 agent 结束后清理仅属于它的连接

这是一个很强的扩展点。

它意味着：

- 不同 subagent 可以拥有不同的外部能力
- agent capability 不需要全部堆在主系统上

如果另一个 LLM 需要复刻机制，但不实现 MCP，也至少应保留“agent 可附加外部工具源”的设计接口。

---

# 15. query()：统一模型循环内核

虽然本文重点不在 `query()` 本身，但要理解：

- subagent 并没有自己单独的推理循环
- `runAgent()` 最终只是把子上下文送入 `query()`

传入的数据包括：

- `messages`
- `systemPrompt`
- `userContext`
- `systemContext`
- `canUseTool`
- `toolUseContext`
- `querySource`
- `maxTurns`

这意味着：

- 如果你已经有一个主 agent 循环，那么 subagent 机制完全可以建立在其之上
- 关键不在于重写循环，而在于正确地“包装出一个 child execution context”

## 15.1 最小消息协议

如果不给 LLM 一份消息协议，它通常会把 transcript、tool use、fork 逻辑实现得不稳定。

推荐最少支持这些消息类型：

```ts
type Message =
  | UserMessage
  | AssistantMessage
  | ProgressMessage
  | AttachmentMessage
  | SystemMessage
```

```ts
type UserMessage = {
  type: 'user'
  uuid: string
  content: ContentBlock[]
  createdAt: number
}

type AssistantMessage = {
  type: 'assistant'
  uuid: string
  content: ContentBlock[]
  createdAt: number
}

type ProgressMessage = {
  type: 'progress'
  uuid: string
  stage: string
  text?: string
  createdAt: number
}

type AttachmentMessage = {
  type: 'attachment'
  uuid: string
  attachmentType: string
  payload: unknown
  createdAt: number
}

type SystemMessage = {
  type: 'system'
  uuid: string
  subtype?: string
  text: string
  createdAt: number
}
```

## 15.2 ContentBlock 协议

```ts
type ContentBlock =
  | { type: 'text'; text: string }
  | { type: 'tool_use'; id: string; name: string; input: unknown }
  | { type: 'tool_result'; tool_use_id: string; content: ContentBlock[] }
```

## 15.3 消息协议不变量

- **[唯一 ID]** 每条消息必须有唯一 `uuid`
- **[tool_result 可回溯]** 每个 `tool_result` 必须指向已有的 `tool_use.id`
- **[顺序稳定]** transcript 中的消息顺序必须与实际执行顺序一致
- **[记录粒度明确]** progress 可不参与 parent linkage，但 user/assistant 应参与
- **[fork 合法]** fork 场景不能留下未闭合的 `tool_use`

---

# 16. transcript 记录：为什么 subagent 必须可追溯

subagent 启动后，会调用：

- `recordSidechainTranscript(...)`
- `writeAgentMetadata(...)`

记录：

- 初始消息
- 后续消息
- `agentType`
- `description`
- `worktreePath`

并且还维护 `lastRecordedUuid`，保证 transcript 的链路连续。

## 16.1 为什么要做 transcript sidechain

因为 subagent 往往不是主对话树上的普通一轮回复，而是一个并行或旁路执行流。

如果不单独记录：

- 无法恢复
- 无法审计
- 无法做总结
- 无法做完成通知
- 无法查看后台 agent 产出

所以如果要复刻，建议至少保留：

- agent id
- 初始输入
- 输出消息流
- metadata
- parent linkage

---

# 17. 后台任务：subagent 为什么需要 task system

当 `shouldRunAsync` 为真时，subagent 不会阻塞当前主流程，而是：

- 先注册后台任务
- 再把 `runAgent()` 交给异步生命周期去消费

后台任务系统需要支持：

- task 注册
- progress 更新
- summary 更新
- output file
- completion/failure 状态切换
- notification
- kill/cancel

这一步非常关键，因为一旦你想让 subagent 真正“能用”，它几乎一定会演进到后台 worker 模式。

## 17.1 为什么同步 agent 也要能 background

一个成熟实现里很值得借鉴的点：

- 同步 agent 在前台运行时，也会先注册 foreground task
- 当运行超过阈值后，用户或系统可以把它 background 掉

这说明任务系统和 agent runner 是松耦合的：

- 一个 agent 可以先前台跑
- 再无缝转后台跑

这是非常值得借鉴的设计。

---

# 18. resume：subagent 的恢复机制

resume 的关键点不是“重新创建一个新的 agent”，而是：

- 恢复原来的 `agentId`
- 恢复原来持久化的 transcript / metadata
- 恢复原来的 worktree / cwd 信息
- 再次挂接到异步执行生命周期上

因此 resume 的正确抽象应当是：

- “恢复 agent task 的运行上下文”

而不是：

- “重新发起一次新的 spawn”

如果要复刻机制，建议在第一次启动 agent 时就写入足够的 metadata，否则后续恢复会很痛苦。

## 18.1 resume 的最小前置条件

如果要支持 resume，至少需要能恢复：

- `agentId`
- `agentType`
- 初始 transcript
- 最近一次状态
- `description`
- `cwd` 或 `worktreePath`
- 恢复所需的最小 runtime options

## 18.2 resume 重建算法

```text
input: taskId or agentId

1. 读取 metadata
2. 读取 transcript sidechain
3. 校验 agentType 与 runtime mode
4. 恢复 cwd / isolation / worktree
5. 重建 ToolUseContext 与 RunAgentParams
6. 重新挂接 task lifecycle
7. 继续消费 runAgent 或基于 transcript 续跑
```

## 18.3 resume 降级策略

- **[metadata 丢失]** 标记为不可恢复并返回明确错误
- **[worktree 不存在]** 回退到主工作目录，或要求显式确认
- **[transcript 损坏]** 至少恢复 metadata，并给出只读失败状态
- **[工具集变化]** 应记录“恢复时环境不同”，避免静默行为漂移

---

# 19. 并发隔离：为什么要用 AsyncLocalStorage

这里通过 `AsyncLocalStorage` 保存：

- `agentId`
- `parentSessionId`
- `agentType`
- `subagentName`
- `isBuiltIn`
- `invokingRequestId`
- `invocationKind`

原因很直接：

- 多个后台 subagent 可以在同一个进程里并发执行
- 如果 attribution 依赖全局变量或共享 state，很容易串线

因此，如果你要支持：

- telemetry
- tracing
- 日志归属
- API 调用归属

就应该给每个 subagent 的异步执行链绑定上下文。

在 Node.js/TypeScript 世界里，`AsyncLocalStorage` 是很自然的选择。

---

# 20. 一个完整例子：Explore 子代理执行流程

下面用一个接近真实使用场景的例子说明整个系统怎么运作。

## 20.1 场景

主 agent 想委派一个搜索型任务：

```text
查找仓库中认证请求在哪里处理，并总结调用链
```

它决定调用 `Explore` subagent。

## 20.2 入口阶段

在调度入口中：

- 解析出 `selectedAgent = EXPLORE_AGENT`
- 将输入 prompt 封装成 `promptMessages`
- 计算 `shouldRunAsync`
- 生成 `workerTools`
- 分配 `earlyAgentId`

## 20.3 运行器准备阶段

调用 `runAgent()` 时，带上：

- `agentDefinition: EXPLORE_AGENT`
- `promptMessages`
- `availableTools: workerTools`
- `toolUseContext`
- `canUseTool`
- `querySource`

## 20.4 runAgent 构造子环境

在统一运行器中：

- 根据 agent 定义计算模型
- 创建正式 `agentId`
- 准备 `initialMessages`
- 裁剪 user/system context
- 解析有效工具集
- 初始化 Explore 需要的 MCP tools（若有）
- 用 `createSubagentContext()` 创建子上下文

这时 Explore agent 已经成为一个独立的执行实体。

## 20.5 query 循环执行

之后进入 `query()`。

模型可能会：

- 调用搜索工具
- 调用读文件工具
- 输出中间进度
- 汇总找到的调用链

而 `runAgent()` 会同步做：

- 转发消息
- 记录 transcript
- 更新指标

## 20.6 收尾阶段

结束后 `runAgent()` 进入 `finally`：

- 清理 hooks
- 清理 MCP 连接
- 清理 file state
- 清理 todos
- 清理 shell task
- 释放 tracing/transcript mapping

如果它是后台任务，还会：

- 更新 completion 状态
- 发送通知
- 提供最终输出结果

---

# 21. 一个完整例子：fork child 的执行流程

再给一个 fork 场景例子。

## 21.1 场景

主 agent 已经分析到一半，并产生了多次工具调用。这时想并行分叉两个方向：

- child A：继续查根因
- child B：验证一个修复猜想

## 21.2 fork 消息构造

`buildForkedMessages()` 会：

- 保留当前父 assistant message
- 找出其中所有 `tool_use`
- 为每个 `tool_use` 生成相同 placeholder 文本的 `tool_result`
- 最后加一段当前 child 的 directive

## 21.2.1 fork message contract

推荐把 fork 输入输出固定成下面语义：

```text
input:
- parent assistant message
- optional parent history slice
- child directive

output:
- inherited assistant context
- synthesized user message containing:
  - placeholder tool_results for all unresolved tool_use blocks
  - one final text block for the child directive
```

## 21.2.2 fork 必须满足的约束

- **[不留悬空 tool_use]** 所有继承来的未闭合 tool_use 都要补上 tool_result
- **[placeholder 稳定]** placeholder 文本应尽量固定，提升 cache 友好性
- **[child 差异后置]** 与 child 相关的差异最好只出现在最后一段 directive
- **[防递归]** fork child 默认不得再次隐式 fork

## 21.3 得到的效果

多个 child 的 prompt 前缀几乎相同，只在最后的任务文本上不同。

这带来两个好处：

- 保证消息结构合法
- 最大化 prompt cache 命中率

这套设计尤其适合“从同一上下文并行拆多个子任务”的场景。

---

# 22. 如果让别的 LLM 从零实现，建议按这 8 个模块拆分

如果目标是让另一个 LLM 读完后实现 subagent 机制，推荐它按下面顺序搭建。

## 模块 1：统一 AgentDefinition

至少支持：

- name/type
- prompt/system prompt
- tools
- permission mode
- max turns
- background flag

## 模块 2：统一 AgentRunner

实现一个 `runAgent()` 风格的运行器：

- 输入 agent definition + child context
- 输出消息流

## 模块 3：child context factory

实现一个 `createSubagentContext()`：

- 克隆 mutable state
- 共享必要回调
- 分配 child agent id
- 继承只读配置

## 模块 4：tool filtering

按 agent 类型和运行模式裁剪工具。

## 模块 5：spawn 入口

实现一个类似 `AgentTool` 的统一入口：

- 选择 agent
- 组装 prompt
- 决定 sync/async
- 决定 isolation

## 模块 6：task lifecycle

实现后台 agent 任务系统：

- register
- progress
- completion
- failure
- cancel
- output

## 模块 7：transcript persistence

至少记录：

- initial messages
- message stream
- metadata
- parent linkage

## 模块 8：resume + async attribution

- 用持久化数据恢复 agent
- 用 async context 绑定当前 agent 身份

---

# 23. 迁移时的关键注意事项

## 23.1 不要让 child 直接复用 parent mutable state

这是最容易出问题的地方。

会导致：

- 缓存串线
- 权限串线
- 日志串线
- 清理困难

## 23.2 不要把 subagent 实现成“只返回最终字符串”

正确抽象应该是：

- 流式消息生成器
- 或可持续上报进度的异步执行器

## 23.3 transcript 一开始就设计好

否则后面要做：

- resume
- notification
- debug
- 审计

都会返工。

## 23.4 sync 与 async 不要拆成两套完全不同的 agent 内核

最好像本仓库一样：

- 同一个 `runAgent()`
- 上层再接 foreground/background lifecycle

## 23.5 fork 最好独立建模

fork 与普通 spawn 的语义差异很大。

如果直接硬塞进普通 spawn，很容易把消息序列和 prompt cache 处理搞乱。

---

# 24. 最小可实现版本建议

如果让别的 LLM 先做一个 MVP，可以先只做：

1. `AgentDefinition`
2. `AgentTool` 风格入口
3. `createSubagentContext()`
4. `runAgent()`
5. `query()` 复用主 agent 内核
6. tool filtering
7. transcript 记录

暂时不做：

- fork prompt cache 优化
- MCP server per agent
- worktree isolation
- resume
- 复杂 summarization

等 MVP 跑通，再加：

- async task system
- resume
- fork
- 外部工具扩展
- tracing/telemetry

---

# 25. 可直接复用的实现蓝图

下面是一份抽象后的蓝图，适合给别的 LLM 当实现提纲：

```text
Subagent Architecture Blueprint

1. Define AgentDefinition
   - identity
   - system prompt factory
   - tool policy
   - permission policy
   - runtime policy

2. Implement spawn entry
   - choose agent
   - build child prompt/messages
   - determine sync/async/isolation
   - allocate agent id

3. Implement child context factory
   - clone mutable caches
   - bind child metadata
   - derive permission state
   - optionally share selected callbacks

4. Implement runAgent
   - resolve model
   - resolve tools
   - construct system/user context
   - attach external tool providers if needed
   - call shared query loop
   - stream messages outward
   - persist transcript
   - cleanup resources in finally

5. Implement async task wrapper
   - register task
   - consume runAgent stream
   - update progress
   - finalize output
   - notify completion/failure

6. Implement persistence and resume
   - write agent metadata
   - record transcript sidechain
   - resume by rebuilding execution context from metadata

7. Implement async attribution
   - use async local context to bind agent identity across awaits
```

---

# 26. 推荐实现顺序

如果要边读边实现，建议顺序如下：

1. 先定义 `AgentDefinition`
2. 再实现内置 agent 与可扩展 agent 注册层
3. 再实现统一的 spawn / dispatch 入口
4. 再实现 fork 子代理分支逻辑
5. 再实现 child context factory
6. 再实现统一的 `runAgent()` 运行器
7. 再实现工具过滤与任务辅助逻辑
8. 再实现 resume 恢复逻辑
9. 最后实现异步上下文 attribution

## 26.1 真正建议的实现优先级

如果你要让另一个 LLM 一次性写出可用版本，推荐分三阶段：

- **[Phase 1]** `AgentDefinition` + `spawn` + `runAgent` + `createSubagentContext`
- **[Phase 2]** tool filtering + permission derivation + transcript persistence
- **[Phase 3]** async task lifecycle + resume + fork + external tool providers

---

# 27. 失败处理与清理策略

如果不把失败路径写清楚，LLM 很容易只实现 happy path。

## 27.1 必须覆盖的失败类型

- **[权限拒绝]** 工具调用被权限系统拒绝
- **[工具失败]** 工具本身抛错或返回错误
- **[模型失败]** LLM API 异常、超时、限流、响应无效
- **[中断]** 用户取消、父 agent 取消、任务系统终止
- **[恢复失败]** metadata 缺失、transcript 损坏、cwd/worktree 不存在
- **[清理失败]** hooks/MCP/background task 清理异常

## 27.2 推荐失败处理原则

- **[主错误优先]** 先保留导致任务失败的原始错误
- **[清理错误降级]** cleanup 出错应记录，但通常不覆盖主错误
- **[终态一致]** 一旦失败或中断，task status 必须进入终态
- **[可恢复性标记]** 对 retryable 与 non-retryable 错误分开标记
- **[转录不中断]** 即使失败，也尽量写入最后一条错误消息与 metadata

## 27.3 finally 阶段推荐清理项

- 关闭 agent 专属外部连接
- 清理 session hooks
- 释放 file read cache
- 释放 child messages 引用
- 清理 task registry 中的临时句柄
- 杀掉遗留后台 shell 或监控任务
- 释放 tracing / attribution / transcript mapping

---

# 28. 最小伪代码骨架

下面这份伪代码不是演示，而是给别的 LLM 直接转成真实代码用的骨架。

## 28.1 spawn 入口骨架

```ts
async function spawnSubagent(input: AgentSpawnInput, parentCtx: ToolUseContext) {
  const agentDef = resolveAgentDefinition(input, parentCtx)
  const shouldRunAsync = decideAsyncMode(input, agentDef, parentCtx)
  const promptMessages = buildPromptMessages(input, parentCtx, agentDef)
  const availableTools = buildWorkerToolPool(parentCtx, agentDef, shouldRunAsync)

  const runParams: RunAgentParams = {
    agentDefinition: agentDef,
    promptMessages,
    toolUseContext: parentCtx,
    availableTools,
    canUseTool: createCanUseTool(parentCtx),
    isAsync: shouldRunAsync,
    querySource: buildQuerySource(agentDef),
    model: input.model,
    description: input.description,
  }

  if (shouldRunAsync) {
    const task = registerAgentTask(input, agentDef, parentCtx)
    persistInitialAgentMetadata(task, input, agentDef, parentCtx)
    void runAsyncLifecycle(task, runParams, parentCtx)
    return { type: 'async_launched', taskId: task.taskId, agentId: task.agentId }
  }

  return await runSyncLifecycle(runParams, parentCtx)
}
```

## 28.2 runAgent 骨架

```ts
async function* runAgent(params: RunAgentParams): AsyncGenerator<Message, void> {
  const agentId = allocateAgentId(params)
  const initialMessages = composeInitialMessages(params)
  const contexts = await buildAgentContexts(params)
  const tools = await resolveAgentTools(params)
  const childCtx = createSubagentContext(params.toolUseContext, {
    agentId,
    messages: initialMessages,
    options: buildChildOptions(params, tools),
    getAppState: buildAgentAppStateView(params),
  })

  await persistInitialTranscript(agentId, initialMessages, params)

  try {
    for await (const msg of query({
      messages: initialMessages,
      systemPrompt: contexts.systemPrompt,
      userContext: contexts.userContext,
      systemContext: contexts.systemContext,
      canUseTool: params.canUseTool,
      toolUseContext: childCtx,
      querySource: params.querySource,
      maxTurns: params.maxTurns ?? params.agentDefinition.maxTurns,
    })) {
      await persistMessage(agentId, msg)
      yield msg
    }
  } finally {
    await cleanupAgentResources(agentId, childCtx, params)
  }
}
```

## 28.3 createSubagentContext 骨架

```ts
function createSubagentContext(parent: ToolUseContext, overrides: Partial<ToolUseContext>): ToolUseContext {
  return {
    agentId: overrides.agentId ?? createAgentId(),
    messages: overrides.messages ?? [],
    readFileState: cloneReadFileState(parent.readFileState),
    abortController: overrides.abortController ?? createChildAbortController(parent.abortController),
    options: overrides.options ?? parent.options,
    getAppState: overrides.getAppState ?? (() => deriveChildAppState(parent.getAppState())),
    setAppState: overrides.setAppState ?? (() => {}),
    setAppStateForTasks: parent.setAppStateForTasks ?? parent.setAppState,
    setResponseLength: overrides.setResponseLength ?? (() => {}),
    pushApiMetricsEntry: parent.pushApiMetricsEntry,
    preserveToolUseResults: overrides.preserveToolUseResults ?? false,
  }
}
```

## 28.4 异步生命周期骨架

```ts
async function runAsyncLifecycle(task: AgentTaskRecord, params: RunAgentParams, parentCtx: ToolUseContext) {
  markTaskRunning(task)

  try {
    const messages: Message[] = []
    for await (const msg of runAgent(params)) {
      messages.push(msg)
      updateTaskProgress(task, msg)
    }

    const result = finalizeAgentResult(messages)
    persistFinalResult(task, result)
    markTaskCompleted(task, result)
    notifyTaskCompleted(task, result)
  } catch (error) {
    persistTaskError(task, error)
    markTaskFailed(task, error)
    notifyTaskFailed(task, error)
  } finally {
    await cleanupTaskRuntime(task, parentCtx)
  }
}
```

## 28.5 resume 骨架

```ts
async function resumeAgent(taskId: string) {
  const metadata = await loadAgentMetadata(taskId)
  const transcript = await loadAgentTranscript(taskId)
  const params = rebuildRunParamsFromMetadata(metadata, transcript)
  return runAsyncLifecycle(rebuildTaskRecord(metadata), params, params.toolUseContext)
}
```

---

# 29. 验收标准

如果要判断“另一个 LLM 是否真的完整复现了机制”，可以用以下标准验收：

## 29.1 最小功能验收

- **[多角色]** 至少能定义 2 个以上不同 agent 角色
- **[可派生]** 主 agent 能显式 spawn 子 agent
- **[上下文隔离]** 子 agent 不会污染父 agent 的文件读取缓存或可变状态
- **[工具过滤]** 不同 agent 能拿到不同工具集
- **[同步执行]** 子 agent 可以前台执行并返回结果
- **[异步执行]** 子 agent 可以注册为后台任务并更新进度
- **[持久化]** 能保存 agent metadata 与 transcript
- **[恢复]** 能从持久化记录中恢复一个后台 agent

## 29.2 工程质量验收

- **[终态清晰]** completed/failed/aborted 明确区分
- **[清理完整]** 失败和取消路径都能清理资源
- **[归属正确]** 并发 agent 的日志、指标、trace 不串线
- **[可扩展]** agent definition 能扩展模型、权限、工具和外部能力

## 29.3 fork 验收

如果实现了 fork，再额外检查：

- **[消息合法]** 补全 tool_result 后消息序列合法
- **[不递归]** fork child 不能无限再 fork
- **[上下文继承]** child 能继承父上下文而不是从零开始
- **[缓存友好]** child prompt 前缀尽量保持稳定

---

# 30. 最终结论

这套代码中的 subagent 机制，本质上是一个：

- 基于统一 LLM query 内核
- 由 AgentDefinition 驱动
- 通过 AgentTool 调度
- 通过 createSubagentContext 做上下文隔离
- 通过 runAgent 执行
- 通过 task/transcript/resume 系统托管生命周期

的通用子代理框架。

如果要让别的 LLM 阅读后复刻，最关键的不是抄某个函数，而是掌握这几个不变原则：

- agent 定义与执行器解耦
- 子上下文隔离优先
- 模型循环统一复用
- 工具与权限按 agent 动态裁剪
- 任务生命周期必须闭环
- transcript/persistence 不是附属功能，而是核心能力
- 并发 agent 必须具备 attribution 隔离

只要把这些原则实现到位，即使换一个代码库、换一种任务系统、换一个模型 SDK，也能做出同等级别的 subagent 机制。
