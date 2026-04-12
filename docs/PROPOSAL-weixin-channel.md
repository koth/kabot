# Proposal: 接入微信 Channel (Weixin Channel Integration)

## 1. 提案概述

本提案描述如何在 OpenClaw 框架中接入微信渠道，实现 AI Agent 与微信用户的消息互通。

**目标**: 实现完整的微信渠道接入，支持消息收发、媒体传输、扫码登录等功能。

**优先级**: High

---

## 2. 技术方案

### 2.1 架构设计

```
┌─────────────────────────────────────────────────────────────────┐
│                     OpenClaw Core                               │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │   Gateway    │───▶│   Pipeline   │───▶│    Agent     │       │
│  │   Manager    │    │   Runtime    │    │   Runtime    │       │
│  └──────────────┘    └──────────────┘    └──────────────┘       │
│         │                                                       │
│         │ Plugin Interface                                       │
│         ▼                                                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │              Weixin Channel Plugin                       │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │    │
│  │  │ Channel  │  │   API    │  │   Auth   │  │   CDN    │ │    │
│  │  │  Plugin  │  │  Layer   │  │  Module  │  │  Handler │ │    │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │    │
│  └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ iLink Protocol
                              ▼
                    ┌─────────────────────┐
                    │  Weixin iLink API   │
                    │  (ilinkai.weixin)   │
                    └─────────────────────┘
                              │
                              ▼
                    ┌─────────────────────┐
                    │   Weixin Server     │
                    └─────────────────────┘
```

### 2.2 核心模块

| 模块 | 职责 | 关键文件 |
|------|------|----------|
| **Channel Plugin** | 实现 OpenClaw ChannelPlugin 接口 | `src/channel.ts` |
| **API Layer** | 与微信 iLink API 通信 | `src/api/api.ts` |
| **Authentication** | 二维码登录、账号管理 | `src/auth/login-qr.ts`, `src/auth/accounts.ts` |
| **Inbound Handler** | 处理入站消息（长轮询） | `src/messaging/inbound.ts`, `src/monitor/monitor.ts` |
| **Outbound Handler** | 处理出站消息（发送） | `src/messaging/send.ts`, `src/messaging/send-media.ts` |
| **CDN Handler** | 媒体文件上传/下载 | `src/cdn/cdn-upload.ts`, `src/cdn/pic-decrypt.ts` |
| **Media Processor** | 媒体类型转换（如 SILK 音频） | `src/media/silk-transcode.ts` |

---

## 3. 功能特性

### 3.1 消息类型支持

#### 入站消息（微信用户 → Agent）
- ✅ 文本消息
- ✅ 图片消息（自动下载解密）
- ✅ 语音消息（支持 SILK 转 WAV）
- ✅ 视频消息
- ✅ 文件消息
- ✅ 引用消息（Reply/Quote）

#### 出站消息（Agent → 微信用户）
- ✅ 文本消息
- ✅ 图片消息（本地上传或远程 URL）
- ✅ 视频消息
- ✅ 文件消息
- ✅ 组合消息（文字 + 媒体）

### 3.2 认证机制

**二维码登录流程**:
```
1. 用户执行: openclaw channels login --channel openclaw-weixin
2. 插件生成二维码并显示在终端
3. 用户使用微信扫描二维码
4. 微信服务器返回 bot_token 和 account_id
5. 插件保存账号信息到本地存储
6. 网关启动长轮询监听消息
```

**账号管理**:
- 支持多账号配置
- 账号信息持久化存储
- 自动清理过期账号
- 支持重新登录刷新 Token

### 3.3 长轮询机制

**技术细节**:
- 使用 `getUpdates` 接口进行长轮询
- 默认超时时间：35 秒
- 支持断点续传（通过 `get_updates_buf`）
- 自动重连和错误恢复

**错误处理**:
- 会话过期自动暂停 5 分钟
- 连续失败 3 次后退避 30 秒
- 支持优雅关闭（AbortSignal）

---

## 4. 配置说明

### 4.1 基本配置

```json
{
  "channels": {
    "openclaw-weixin": {
      "enabled": true,
      "accounts": {
        "account-id-1": {
          "name": "Bot Account 1",
          "enabled": true,
          "cdnBaseUrl": "https://novac2c.cdn.weixin.qq.com/c2c"
        }
      }
    }
  }
}
```

### 4.2 环境变量

| 变量名 | 说明 | 默认值 |
|--------|------|--------|
| `OPENCLAW_CONFIG` | OpenClaw 配置文件路径 | `~/.openclaw/openclaw.json` |

---

## 5. API 规范

### 5.1 核心接口

**iLink API 端点**:
- `POST /ilink/bot/getupdates` - 获取消息（长轮询）
- `POST /ilink/bot/sendmessage` - 发送消息
- `POST /ilink/bot/getuploadurl` - 获取上传 URL
- `POST /ilink/bot/getconfig` - 获取配置（含 typing_ticket）
- `POST /ilink/bot/sendtyping` - 发送输入状态

**认证方式**:
- Header: `Authorization: Bearer {bot_token}`
- Header: `AuthorizationType: ilink_bot_token`
- Header: `iLink-App-Id: bot`

### 5.2 消息格式

**入站消息结构**:
```typescript
interface WeixinMessage {
  from_user_id: string;      // 发送者 ID (xxx@im.wechat)
  to_user_id: string;        // 接收者 ID
  client_id: string;         // 消息唯一 ID
  message_type: MessageType;
  message_state: MessageState;
  item_list: MessageItem[];  // 消息内容项
  context_token: string;     // 上下文令牌（发送回复时需要）
  create_time_ms: number;
}
```

---

## 6. 部署指南

### 6.1 前置要求

- Node.js >= 22
- OpenClaw >= 2026.3.22
- 微信 iLink 开发者权限

### 6.2 安装步骤

```bash
# 1. 安装插件
npm install @tencent-weixin/openclaw-weixin

# 2. 登录微信账号
openclaw channels login --channel openclaw-weixin

# 3. 启动网关
openclaw gateway start
```

### 6.3 目录结构

```
~/.openclaw/
├── openclaw.json                    # 主配置文件
└── state/
    └── openclaw-weixin/
        ├── accounts.json            # 账号索引
        └── accounts/
            ├── {accountId}.json           # 账号凭证
            ├── {accountId}.sync.json      # 同步缓冲区
            └── {accountId}.context-tokens.json  # 上下文令牌
```

---

## 7. 安全考虑

### 7.1 数据安全

- Token 存储：本地 JSON 文件，权限 0o600
- 媒体文件：下载后本地解密，使用 AES-ECB 加密
- 日志脱敏：敏感信息（token、uin）在日志中打码

### 7.2 访问控制

- 支持 `allowFrom` 白名单过滤
- 框架级命令授权验证
- 账号级别启用/禁用控制

---

## 8. 实现状态

| 功能 | 状态 | 备注 |
|------|------|------|
| 基础消息收发 | ✅ 已实现 | 文本、媒体 |
| 二维码登录 | ✅ 已实现 | 终端显示二维码 |
| 长轮询监听 | ✅ 已实现 | 断点续传、自动重连 |
| 媒体传输 | ✅ 已实现 | 加密上传/下载 |
| 多账号管理 | ✅ 已实现 | 支持多账号配置 |
| 语音转码 | ✅ 已实现 | SILK → WAV |
| 群组消息 | ❌ 未支持 | 仅支持私聊 |
| 消息撤回 | ❌ 未支持 | 未来可扩展 |
| 消息已读回执 | ❌ 未支持 | 未来可扩展 |

---

## 9. 最佳实践

### 9.1 Agent Prompt 提示词

插件已内置以下提示词建议：

1. **媒体发送**: 使用 message tool 的 `action='send'` 和 `media` 参数
2. **图片搜索**: 先搜索 HTTPS URL，直接传入 media 参数，不要下载
3. **文件路径**: 使用绝对路径（如 `/tmp/photo.png`），不要使用相对路径
4. **定时任务**: Cron 任务必须设置 `delivery.to` 和 `delivery.accountId`
5. **MEDIA 指令**: MEDIA: 标签必须单独一行

### 9.2 性能优化

- 文本分块限制：4000 字符/条
- 流式响应：支持块级合并（200 字符 / 3 秒空闲）
- 上下文缓存：内存 + 磁盘持久化

---

## 10. 参考链接

- **OpenClaw Plugin SDK**: 提供 `ChannelPlugin` 接口和运行时环境
- **iLink 协议文档**: 微信 Bot 开发协议规范
- **源码地址**: `@tencent-weixin/openclaw-weixin`

---

**提案版本**: 1.0  
**最后更新**: 2026-03-28  
**作者**: OpenClaw Team
