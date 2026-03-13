## Why

当前 `SandboxExecutor` 仅在 Unix-like 系统上实现命令执行，在 Windows 上直接返回不支持。这会导致依赖沙箱执行的工具能力在 Windows 上不可用，包括交互式命令执行、后台任务启动，以及通过外部命令驱动的音频处理与播放能力，削弱 Kabot 在跨平台开发环境中的可用性。

随着项目需要同时支持 Unix 和 Windows 作为开发与运行宿主，工具层和沙箱层需要提供一致、可预期的行为边界，避免因为平台差异导致同一配置在不同系统上出现能力缺失或失败模式不一致。

## What Changes

- 为 Kabot 新增一个跨平台的工具执行与沙箱能力，覆盖 Unix 和 Windows 宿主环境。
- 为沙箱执行器定义统一的跨平台行为，包括工作目录、超时、stdout/stderr 收集、阻断策略与退出状态语义。
- 要求所有依赖沙箱执行的内置工具在 Unix 与 Windows 上都能工作，或在受策略限制时返回一致、可诊断的错误结果。
- 明确平台相关命令解释器与进程终止行为，确保 `shell`、`spawn`、以及依赖外部命令的工具在双平台上行为一致。
- 为跨平台沙箱与工具执行补充测试覆盖，验证 Unix/Windows 的兼容性与策略约束。

## Capabilities

### New Capabilities
- `cross-platform-tools-sandbox`: 定义 Kabot 工具执行与沙箱层在 Unix 和 Windows 宿主上的统一行为、限制策略和兼容性要求

### Modified Capabilities

## Impact

- 受影响代码主要位于 `src/sandbox/sandbox_executor.cpp`、`src/sandbox/sandbox_executor.hpp`。
- 受影响工具调用方包括 `src/agent/tools/shell.cpp`、`src/agent/tools/spawn.cpp`，以及通过 `SandboxExecutor` 调用外部命令的其他工具，例如 `src/agent/tools/tts.cpp`。
- 需要补充或调整与沙箱、工具执行、平台兼容性相关的测试。
- 不涉及现有 OpenSpec 能力的行为变更归档，新增能力以独立 spec 进行描述。
