# Claude Code Tool System Architecture

## Overview

This document summarizes the `tool` system in this repository, including:

- the core tool abstraction
- how tools are registered and executed
- the responsibilities of the built-in tools
- how tools are layered from foundational primitives to higher-level orchestration
- which tools are the most important and most fundamental

The goal is to provide a code-reading guide for understanding how Claude Code turns model tool calls into concrete actions.

---

## 1. What a Tool Is in This Codebase

In this repository, a `tool` is a standardized capability that the model can invoke during a turn.

A tool is not just a function. It is a structured runtime object with:

- a stable name
- an input schema
- permission behavior
- validation logic
- execution logic
- result serialization
- UI rendering hooks
- concurrency and mutability semantics

The central definition of the tool abstraction lives in the core tool type layer.

### Core tool interface

The `Tool` type is the canonical abstraction for all tools.

A tool typically defines:

- `name`
- `inputSchema`
- `outputSchema`
- `call()`
- `description()`
- `prompt()`
- `validateInput()`
- `checkPermissions()`
- `isConcurrencySafe()`
- `isReadOnly()`
- `isDestructive()`
- `mapToolResultToToolResultBlockParam()`
- several UI rendering functions

This means every tool in the system is expected to answer several questions:

- What arguments does it accept?
- Is the input valid?
- Does it require approval?
- Can it run in parallel?
- Is it read-only or mutating?
- How should the result be shown to the model?
- How should the result be shown to the user?

### `buildTool()`

`buildTool()` is the helper used to construct tools.

It fills in safe defaults for common properties, including:

- `isEnabled()` defaults to `true`
- `isConcurrencySafe()` defaults to `false`
- `isReadOnly()` defaults to `false`
- `isDestructive()` defaults to `false`
- `checkPermissions()` defaults to allow-through behavior
- `toAutoClassifierInput()` defaults to empty string
- `userFacingName()` defaults to the tool name

This makes `buildTool()` the standard constructor/factory layer for the tool system.

---

## 2. Where Tools Are Registered

The built-in tool registry has a single source of truth for assembling the built-in tool set.

### Key functions

- `getAllBaseTools()`
  - returns the exhaustive list of built-in tools available in the current environment
- `getTools()`
  - filters the built-in tools based on mode, permissions, feature flags, REPL/simple mode, and availability
- `assembleToolPool()`
  - combines built-in tools with MCP tools
- `getMergedTools()`
  - gets built-in tools together with MCP tools

### Important design point

The registry layer is not just a list of imports.
It is the place where the runtime decides which tools exist in the current session.

That means this file acts as:

- the built-in tool registry
- the built-in tool filter
- the built-in + MCP assembly layer

---

## 3. How Tool Execution Works

Tool execution is split into orchestration and per-tool execution.

### 3.1 Tool orchestration

Key function:

- `runTools()`

Responsibilities:

- receives tool calls emitted by the model
- partitions them into batches
- checks whether a tool call is concurrency-safe
- runs read-only / concurrency-safe batches in parallel
- runs non-read-only / unsafe batches serially
- updates tool-use context across the execution flow

This file is the scheduler for tool calls.

### 3.2 Tool execution engine

Responsibilities:

- find the requested tool
- parse input with `inputSchema.safeParse`
- run `validateInput()`
- run permission checks
- run pre-tool hooks
- invoke `tool.call()`
- run post-tool hooks
- serialize the result into `tool_result`
- emit progress / error / attachment messages

This file is the runtime execution engine for a single tool invocation.

### 3.3 Tool hooks

Responsibilities:

- pre-tool hook execution
- post-tool hook execution
- failure hook execution
- stop / block / additional context handling

Hooks sit between the raw tool call and the actual tool implementation.

---

## 4. High-Level Tool Execution Flow

At a high level, a tool call follows this lifecycle:

1. The model emits a `tool_use`
2. The runtime resolves the tool by name
3. The input is parsed against the tool schema
4. The input is validated with tool-specific logic
5. Permissions are checked
6. Pre-tool hooks run
7. The tool's `call()` method executes
8. Post-tool hooks run
9. The result is serialized back into a `tool_result`
10. The result and progress are rendered in the UI

So the system is not simply “call a function by name”.
It is a controlled pipeline with:

- schema enforcement
- permission checks
- hook interception
- concurrency control
- result shaping
- UI rendering

---

## 5. Main Built-In Tools

The main built-in tool list includes the following tools, subject to environment flags and runtime filtering:

- `AgentTool`
- `TaskOutputTool`
- `BashTool`
- `GlobTool`
- `GrepTool`
- `ExitPlanModeV2Tool`
- `FileReadTool`
- `FileEditTool`
- `FileWriteTool`
- `NotebookEditTool`
- `WebFetchTool`
- `TodoWriteTool`
- `WebSearchTool`
- `TaskStopTool`
- `AskUserQuestionTool`
- `SkillTool`
- `EnterPlanModeTool`
- `ConfigTool`
- `TungstenTool`
- `WebBrowserTool`
- `TaskCreateTool`
- `TaskGetTool`
- `TaskUpdateTool`
- `TaskListTool`
- `CtxInspectTool`
- `TerminalCaptureTool`
- `LSPTool`
- `EnterWorktreeTool`
- `ExitWorktreeTool`
- `SendMessageTool`
- `ListPeersTool`
- `TeamCreateTool`
- `TeamDeleteTool`
- `VerifyPlanExecutionTool`
- `REPLTool`
- `WorkflowTool`
- `SleepTool`
- `CronCreateTool`
- `CronDeleteTool`
- `CronListTool`
- `RemoteTriggerTool`
- `MonitorTool`
- `BriefTool`
- `SendUserFileTool`
- `PushNotificationTool`
- `SubscribePRTool`
- `PowerShellTool`
- `SnipTool`
- `TestingPermissionTool`
- `ListMcpResourcesTool`
- `ReadMcpResourceTool`
- `ToolSearchTool`

Not all of them are always present. Many are gated by:

- feature flags
- environment variables
- user type
- REPL/simple mode
- worktree mode
- MCP availability

---

## 6. Functional Categorization of Tools

A useful way to understand the tool set is to separate tools by responsibility.

## 6.1 Core local code and shell primitives

These are the foundational code-manipulation tools.

- `BashTool`
  - executes shell commands
  - can run read-only commands, build commands, git commands, tests, search commands, long-running tasks, and background jobs
- `FileReadTool`
  - reads files from disk
  - supports text files, notebooks, images, and PDFs
- `FileEditTool`
  - performs precise in-place text replacement edits in existing files
- `FileWriteTool`
  - creates or overwrites files
- `NotebookEditTool`
  - edits Jupyter notebook cells
- `GlobTool`
  - file discovery by glob patterns
- `GrepTool`
  - content search in files
- `PowerShellTool`
  - PowerShell-specific command execution when enabled

These are the tools most directly involved in software engineering work over a local workspace.

## 6.2 User interaction and task control tools

These tools manage task flow and user interaction rather than directly manipulating source code.

- `AskUserQuestionTool`
  - asks the user a structured question with options
- `TodoWriteTool`
  - updates the todo/task list shown to the user
- `TaskStopTool`
  - stops tasks / tool-driven task flow
- `TaskOutputTool`
  - returns task output and completion data
- `EnterPlanModeTool`
  - enters plan mode
- `ExitPlanModeV2Tool`
  - exits plan mode
- `BriefTool`
  - controls concise/brief output behavior

## 6.3 Web and external information tools

These expand the model's knowledge beyond the local repository.

- `WebFetchTool`
  - fetches content from a URL
- `WebSearchTool`
  - performs web search
- `WebBrowserTool`
  - interactive browser-like web capability when enabled
- `ListMcpResourcesTool`
  - lists static resources exposed by an MCP server
- `ReadMcpResourceTool`
  - reads a specific MCP resource

## 6.4 Agent / orchestration / delegation tools

These tools coordinate other capabilities rather than directly editing local files.

- `AgentTool`
  - spawns or delegates to sub-agents
- `SkillTool`
  - executes reusable skills/workflows
- `SendMessageTool`
  - message passing across agents
- `TeamCreateTool`
  - creates a team of agents/workers
- `TeamDeleteTool`
  - tears down team agent structures
- `ToolSearchTool`
  - searches deferred tools so the model can discover and select them

## 6.5 Task data model tools

These look like a more structured task-management layer.

- `TaskCreateTool`
  - create a task record
- `TaskGetTool`
  - fetch task details
- `TaskUpdateTool`
  - update task state
- `TaskListTool`
  - list tasks

## 6.6 IDE / language / environment integration tools

These connect Claude Code to broader developer tooling and environment state.

- `LSPTool`
  - integrates with language server functionality
- `TerminalCaptureTool`
  - reads terminal content/capture data
- `CtxInspectTool`
  - context inspection tooling
- `ConfigTool`
  - configuration management
- `WorkflowTool`
  - workflow script execution
- `EnterWorktreeTool`
  - enter git worktree mode
- `ExitWorktreeTool`
  - exit git worktree mode
- `TestingPermissionTool`
  - test-only permission path tool

## 6.7 Scheduling, remote trigger, and background automation tools

These support longer-lived or proactive automation flows.

- `SleepTool`
  - wait/suspend in proactive mode
- `CronCreateTool`
  - create scheduled triggers
- `CronDeleteTool`
  - delete scheduled triggers
- `CronListTool`
  - list scheduled triggers
- `RemoteTriggerTool`
  - remote trigger execution
- `MonitorTool`
  - streaming/polling/background monitoring tool when enabled
- `PushNotificationTool`
  - push notifications
- `SubscribePRTool`
  - subscribe to PR events/webhooks
- `SendUserFileTool`
  - send a file to the user in Kairos-related modes

## 6.8 MCP abstraction tools

These exist to bridge to the Model Context Protocol ecosystem.

- `MCPTool`
  - the generic MCP tool shell / template abstraction
- `ListMcpResourcesTool`
  - resource listing
- `ReadMcpResourceTool`
  - resource reading
- `McpAuthTool`
  - MCP auth-related support

---

## 7. Detailed Notes on the Most Important Tools

## 7.1 `BashTool`

### Definition

`BashTool` is the shell execution tool.
It is one of the most capable tools in the entire system.

### Main responsibilities

- execute shell commands
- distinguish read-only vs mutating commands
- support command descriptions
- handle background tasks
- handle sandboxed execution
- report progress during long-running commands
- serialize stdout/stderr to tool results
- persist oversized output to disk when needed
- integrate with permission and classifier logic

### Why it matters

`BashTool` is the general-purpose escape hatch of the system.
It can do:

- file search
- grep/find-like operations
- test execution
- builds
- git operations
- package manager commands
- local scripting
- service startup

It is the broadest built-in tool in practical capability.

---

## 7.2 `FileReadTool`

### Definition

`FileReadTool` is the canonical file inspection tool.
It reads local files in a controlled, typed way.

### Main responsibilities

- read text files
- read images
- read PDFs
- read Jupyter notebooks
- enforce file size / token limits
- support partial reads via `offset` and `limit`
- support PDF page-range reads
- store read-state metadata
- perform path normalization and permission-aware matching
- deduplicate repeated reads when possible

### Why it matters

This is the primary mechanism the model uses to inspect source files.
It is foundational to code understanding.

A very important design invariant appears in the edit path:

- files are expected to be read before they are edited

That makes `FileReadTool` more fundamental than any mutating file tool.

---

## 7.3 `FileEditTool`

### Definition

`FileEditTool` is the precise in-place editing tool for existing files.
It performs string-based replacement with multiple safety checks.

### Main responsibilities

- validate that a file exists or that creation semantics are valid
- ensure the file has been read before editing
- detect whether the file changed since it was read
- locate the exact string to replace
- handle quote normalization
- support `replace_all`
- reject ambiguous edits unless explicitly allowed
- write changes to disk atomically
- update LSP and editor integrations after edits
- update internal read-state

### Why it matters

This is the main structured code-editing primitive.
It is safer and more constrained than free-form shell mutation.

The tool’s design shows several important invariants:

- read-before-edit discipline
- stale-write prevention
- exact-match or explicit multi-match replacement
- notebook edits must use the notebook-specific tool

---

## 7.4 `FileWriteTool`

### Definition

`FileWriteTool` handles file creation or full overwrite.
It complements `FileEditTool`.

### Main responsibilities

- create new files
- write full contents
- handle overwrite-style mutations
- enforce permission and schema constraints

### Why it matters

It is essential for creating new files or replacing entire contents, but it is usually less precise than `FileEditTool` for incremental code modification.

---

## 7.5 `GlobTool` and `GrepTool`

### Definition

These are dedicated search tools.

### Main responsibilities

- `GlobTool`
  - filename/path matching and file discovery
- `GrepTool`
  - content matching within files

### Why they matter

They are highly useful for codebase exploration, but they are slightly less fundamental than `Read/Edit/Bash` because:

- some environments can replace them with embedded search capabilities
- some search behavior can be approximated with shell commands

They are best understood as specialized search accelerators.

---

## 7.6 `ToolSearchTool`

### Definition

`ToolSearchTool` is a tool-discovery layer.
It exists so the model can search for deferred tools when the full schema of every tool is not initially loaded.

### Main responsibilities

- search deferred tools by keyword
- support direct `select:<tool_name>` lookup
- score tools based on name, description, and `searchHint`
- return tool references instead of directly executing the target tool

### Why it matters

It is important for scale and prompt efficiency, but it is not a foundational execution primitive.
It is a discovery/meta layer built on top of the broader tool ecosystem.

---

## 7.7 `MCPTool`

### Definition

`MCPTool` is the generic wrapper/template abstraction for tools exposed by MCP servers.

### Main responsibilities

- provide a standardized tool shape for MCP-backed capabilities
- allow external tool definitions to plug into the same runtime model
- bridge MCP tool names, schemas, permissions, and outputs into the internal tool system

### Why it matters

It is strategically important because it lets the system extend beyond built-in tools.
However, it is not one of the most fundamental local coding primitives.
It is better understood as an extensibility bridge.

---

## 8. Which Tools Are the Most Fundamental?

There are two different ways to answer this.

## 8.1 Most fundamental infrastructure files

These are not user-facing tools themselves, but they are the foundation of the entire tool system.

- core tool abstraction layer
  - canonical tool abstraction and defaults
- tool registry and assembly layer
  - built-in tool registry and assembly logic
- tool execution engine
  - single-tool runtime execution engine
- tool orchestration layer
  - tool scheduling, batching, and concurrency control

Without these layers, the tool system does not exist as a coherent runtime.

## 8.2 Most fundamental concrete tools

The most important concrete tools are:

- `BashTool`
- `FileReadTool`
- `FileEditTool`
- `FileWriteTool`

And if a strict minimal essential set is required, the strongest answer is:

- `BashTool`
- `FileReadTool`
- `FileEditTool`

### Why these three

Together they form the minimum practical software-engineering loop:

- inspect the repository
- read source code
- make targeted code changes

This is also strongly supported by the simplified tool set, where simple mode keeps:

- `BashTool`
- `FileReadTool`
- `FileEditTool`

That is a strong signal from the codebase itself that these are considered the essential base tools.

---

## 9. Why These Are More Foundational Than Other Tools

### `BashTool` is foundational because:

- it is the broadest execution primitive
- it can cover shell-based search, build, test, git, and system operations
- it acts as a universal fallback capability

### `FileReadTool` is foundational because:

- all code understanding depends on reliable file inspection
- edit flows depend on prior read-state
- it provides a safer and more structured alternative to ad hoc shell reads

### `FileEditTool` is foundational because:

- it is the main structured mutation primitive
- it enforces disciplined and safe edits
- it represents the standard path for changing existing code

### `FileWriteTool` is foundational but slightly secondary because:

- it is essential for file creation and full overwrite
- but incremental source modification usually routes more naturally through `FileEditTool`

### Why other tools are less foundational

- `ToolSearchTool`
  - discovery layer, not an execution primitive
- `MCPTool`
  - extensibility layer, not a local code primitive
- `AgentTool` and `SkillTool`
  - orchestration/meta tools built on lower-level capabilities
- `WebFetchTool` and `WebSearchTool`
  - external knowledge tools, useful but not required for core local coding
- `AskUserQuestionTool` and task tools
  - workflow control rather than code manipulation

---

## 10. Practical Reading Order for Understanding the Tool System

If you want to understand the tool system from the ground up, a good reading order is:

1. Tool abstraction
  - understand the canonical `Tool` abstraction
2. Tool registry
  - see how tools are registered and filtered
3. Tool orchestration
  - understand batching and concurrency decisions
4. Tool execution engine
  - understand runtime validation, permissions, hooks, and result handling
5. `BashTool`
  - understand the broadest execution primitive
6. `FileReadTool`
  - understand structured repository inspection
7. `FileEditTool`
  - understand safe source-code mutation
8. `FileWriteTool`
  - understand file creation/overwrite semantics
9. `ToolSearchTool`
  - understand deferred-tool discovery
10. `MCPTool`
  - understand extensibility through MCP

---

## 11. Summary

The tool system in this repository is built around a clean layered design:

- **abstraction layer**
  - core tool abstraction
- **registry / assembly layer**
  - tool registry and assembly
- **execution / orchestration layer**
  - tool execution and orchestration
- **concrete capability layer**
  - individual tools such as `BashTool`, `FileReadTool`, and `FileEditTool`

Among the many built-in tools, the most foundational concrete tools are:

- `BashTool`
- `FileReadTool`
- `FileEditTool`
- `FileWriteTool`

And if a strict minimal essential set is required, the strongest answer is:

- `BashTool`
- `FileReadTool`
- `FileEditTool`

These represent the minimum complete loop for codebase exploration and modification.
