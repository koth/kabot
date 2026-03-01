---
name: cron
description: Schedule reminders and recurring tasks.
metadata: {"kabot":{"always":true}}
---

# Cron

Use the `cron` tool to schedule reminders or recurring tasks.

## Three Modes

1. **Reminder** - message is sent directly to user, always set mode to 'reminder'
2. **Task** - message is a task description, agent executes and sends result
3. **One-time** - runs once at a specific time, then auto-deletes

### Mode Selection Rules (重要)

- **默认是 Task**：除非用户明确说“提醒/提醒我/通知我/提醒我…”，否则按 Task 处理。
- **Reminder 仅用于纯通知**：消息内容是“要说给用户听的一句话”，不需要执行任何动作。
- **包含动作/查询/执行**（如：查天气/整理/生成/检查/统计/汇总/发邮件/拉数据/跑脚本）一律视为 Task。
- **延迟提醒**必须显式设置 `mode="reminder"`；否则仍按 Task。
- 用户既要“执行任务”又要“通知结果”，仍用 Task（不设置 reminder）。

## Examples


One-time reminder(compute ISO datetime from current time):
```
cron(action="add", message="Go to sleep!", at="<ISO datetime>", mode="reminder")
```

Repeated reminder:
```
cron(action="add", message="Time to take a break!", every_seconds=1200, mode="reminder")
```

Dynamic task (agent executes each time):
```
cron(action="add", message="Check HKUDS/nanobot GitHub stars and report", every_seconds=600)
```

One-time scheduled task (compute ISO datetime from current time):
```
cron(action="add", message="Remind me about the meeting", at="<ISO datetime>")
```

自然语言延迟提醒（不要立刻执行查询天气，只创建一次性任务）：
```
cron(action="add", message="播放当前天气情况", at="<ISO datetime>")
```

自然语言延迟任务（需要执行动作/查询）：
```
cron(action="add", message="统计一下今天的订单数并发给我", at="<ISO datetime>")
```

延迟发消息：
```
cron(action="add", message="<要发的消息>", at="<ISO datetime>", mode="reminder")
```

Timezone-aware cron:
```
cron(action="add", message="Morning standup", cron_expr="0 9 * * 1-5", tz="America/Vancouver")
```

List/remove:
```
cron(action="list")
cron(action="remove", job_id="abc123")
```

## Time Expressions

| User says | Parameters |
|-----------|------------|
| every 20 minutes | every_seconds: 1200 |
| every hour | every_seconds: 3600 |
| every day at 8am | cron_expr: "0 8 * * *" |
| weekdays at 5pm | cron_expr: "0 17 * * 1-5" |
| 9am Vancouver time daily | cron_expr: "0 9 * * *", tz: "America/Vancouver" |
| at a specific time | at: ISO datetime string (compute from current time) |
| “2分钟后，播放当前天气情况” | 只创建一次性任务；message: "播放当前天气情况"；at: 当前时间+2分钟 |
| “5分钟后提醒我喝水” | 只创建一次性任务；message: "该喝水了"；at: 当前时间+5分钟；mode: reminder |

## Timezone

Use `tz` with `cron_expr` to schedule in a specific IANA timezone. Without `tz`, the server's local timezone is used.
