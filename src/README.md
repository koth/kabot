# Kabot C++

此目录为 kabot 的 C++ 迁移实现，目录结构与 `kabot/` 对齐。

## 结构

- `agent/` 代理核心
- `bus/` 消息总线
- `channels/` 渠道（当前仅规划 Telegram）
- `cli/` 命令行入口
- `config/` 配置加载与结构
- `cron/` 计划任务
- `heartbeat/` 心跳任务
- `providers/` LLM Provider 抽象
- `session/` 会话与状态
- `skills/` 技能与工具
- `utils/` 通用工具
