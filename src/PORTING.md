# Kabot C++ 迁移清单

## 迁移范围

本清单用于记录 `kabot/` 与 `cpp/` 的模块映射与差异说明。

## 模块映射

| 源模块 (kabot/) | 目标模块 (cpp/) | 状态 | 差异/说明 |
|---|---|---|---|
| agent | agent | partial | 已生成入口文件，核心逻辑待补充 |
| bus | bus | partial | 已生成事件/总线骨架，实现待补充 |
| channels | channels | partial | 仅保留 Telegram 渠道，WhatsApp 暂不迁移 |
| cli | cli | partial | CLI 入口已生成，功能待补充 |
| config | config | partial | 配置结构已生成，加载逻辑待补充 |
| cron | cron | partial | 计划任务接口已生成，逻辑待补充 |
| heartbeat | heartbeat | partial | 心跳接口已生成，逻辑待补充 |
| providers | providers | partial | Provider 抽象已生成，具体实现待补充 |
| session | session | partial | 会话类型已生成，管理逻辑待补充 |
| skills | skills | partial | 技能入口已生成，加载逻辑待补充 |
| utils | utils | partial | 通用工具已生成，扩展待补充 |

## 说明

- 状态取值建议：planned / migrated / partial / skipped
- 若为 partial 或 skipped，必须填写差异原因
