# Kabot C++


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

## Cron 工具

对话中可使用 `cron` 工具创建/管理定时任务。

### 示例

固定提醒：
```
cron(action="add", message="Time to take a break!", every_seconds=1200)
```

动态任务（由 agent 执行并返回结果）：
```
cron(action="add", message="Check HKUDS/nanobot GitHub stars and report", every_seconds=600)
```

一次性任务（到点执行后自动删除）：
```
cron(action="add", message="Remind me about the meeting", at="2026-02-22T22:05:00")
```

带时区的 Cron 表达式：
```
cron(action="add", message="Morning standup", cron_expr="0 9 * * 1-5", tz="America/Vancouver")
```

列表/删除：
```
cron(action="list")
cron(action="remove", job_id="abc123")
```
