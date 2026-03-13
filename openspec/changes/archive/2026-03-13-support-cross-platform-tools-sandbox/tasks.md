## 1. Sandbox Executor Cross-Platform Foundation

- [x] 1.1 重构 `src/sandbox/sandbox_executor.cpp`，抽离平台无关的阻断策略、结果封装与输出收集辅助逻辑
- [x] 1.2 为 `SandboxExecutor` 增加 Windows 命令执行分支，支持工作目录、stdout/stderr 收集和基础退出码返回
- [x] 1.3 对齐 Unix 与 Windows 的超时终止语义和 `ExecResult` 字段合同，确保 `timed_out`、`blocked`、`exit_code` 行为一致
- [x] 1.4 将危险命令阻断策略提升为跨平台预检查，并补充可诊断错误信息

## 2. Tool Compatibility Updates

- [x] 2.1 校验并必要时调整 `src/agent/tools/shell.cpp`，确保在 Windows 上消费标准化沙箱结果并返回一致错误文本
- [x] 2.2 校验并必要时调整 `src/agent/tools/spawn.cpp`，确保后台任务在 Windows 上通过统一沙箱接口执行并记录结果
- [x] 2.3 盘点并修正其他依赖 `SandboxExecutor` 的工具调用方（如 `src/agent/tools/tts.cpp`），消除 Unix-only 假设

## 3. Verification

- [x] 3.1 为沙箱执行器补充测试，覆盖正常执行、工作目录、stdout/stderr 分离、阻断策略与超时行为
- [x] 3.2 为工具层补充或更新测试，验证 `shell`、`spawn` 及至少一个外部命令依赖工具在统一执行合同下的行为
- [ ] 3.3 在 Unix 与 Windows 环境分别完成一次验证，确认不再出现 “SandboxExecutor is not supported on Windows” 的失败路径
