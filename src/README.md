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

## Windows 编译

### 环境要求

- Windows 10/11
- Visual Studio 2022 或 Build Tools 2022，安装 `Desktop development with C++`
- CMake 3.20+
- Git
- PowerShell

### 使用 vcpkg 初始化依赖

先获取并初始化 vcpkg：

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
```

安装当前项目需要的依赖：

```powershell
C:\dev\vcpkg\vcpkg.exe install openssl curl sqlite3 boost-filesystem boost-system boost-process boost-beast boost-property-tree boost-algorithm boost-lexical-cast boost-variant --triplet x64-windows
```

如果启用了 Lark 渠道，需要 `Boost.Beast`。如果启用了 Telegram 渠道，`tgbot-cpp` 还会使用 `Boost.PropertyTree`、`Boost.Algorithm`、`Boost.LexicalCast`、`Boost.Variant`。上面的命令已包含这些依赖。

### 配置项目

当前仓库的 CMake 入口在 `src/` 目录，因此需要把 `src` 作为源码目录：

```powershell
cmake -S g:\work\kabot\src -B g:\work\kabot\build ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
```

如果你是在仓库根目录执行，也可以写成相对路径：

```powershell
cmake -S .\src -B .\build ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
```

如果你安装的是静态 triplet，配置时必须显式指定同一个 triplet：

```powershell
cmake -S .\src -B .\build ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
```

### 编译

```powershell
cmake --build g:\work\kabot\build --config Release
```

如果你在仓库根目录执行：

```powershell
cmake --build .\build --config Release
```

### 常见问题

#### 1. 找不到 OpenSSL、CURL、SQLite3 或 Boost

确认依赖已经通过 vcpkg 安装，并且 `CMAKE_TOOLCHAIN_FILE` 指向：

```text
C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
```

如果报错缺少以下头文件，通常表示还没有安装对应的 Boost 子包：

- `boost/beast/*` 对应 `boost-beast`
- `boost/property_tree/*` 对应 `boost-property-tree`
- `boost/algorithm/*` 对应 `boost-algorithm`
- `boost/lexical_cast.hpp` 对应 `boost-lexical-cast`
- `boost/variant.hpp` 对应 `boost-variant`

#### 2. 生成器不匹配

如果本机没有完整 Visual Studio 2022，可先确认以下命令可用：

```powershell
cmake --help
```

并检查生成器列表里是否包含：

```text
Visual Studio 17 2022
```

#### 3. 想使用静态库

可改为安装静态 triplet：

```powershell
C:\dev\vcpkg\vcpkg.exe install openssl curl sqlite3 boost-filesystem boost-system boost-process boost-beast boost-property-tree boost-algorithm boost-lexical-cast boost-variant --triplet x64-windows-static
```

并在配置时加上：

```powershell
-DVCPKG_TARGET_TRIPLET=x64-windows-static
```

如果不传这个参数，CMake 通常会继续使用默认的 `x64-windows`，从而出现“依赖已经安装但编译时仍然找不到头文件或库”的问题。

#### 4. 重新配置失败

删除旧的 `build/` 目录后重新执行 `cmake -S ... -B ...`，避免缓存旧的工具链或依赖路径。

### 最小可用流程

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
C:\dev\vcpkg\vcpkg.exe install openssl curl sqlite3 boost-filesystem boost-system boost-process boost-beast boost-property-tree boost-algorithm boost-lexical-cast boost-variant --triplet x64-windows
cmake -S .\src -B .\build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build .\build --config Release
```

### QQBot 依赖与启用

当前 `KABOT_ENABLE_QQBOT` 默认开启，CMake 会通过 `FetchContent` 拉取 `qqbot_cpp`，并生成 `qqbot_sdk` 静态库。

如果你使用静态 triplet，建议继续使用：

```powershell
cmake -S .\src -B .\build ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DKABOT_ENABLE_QQBOT=ON
```

QQBot 依赖与现有工程一致，主要仍然依赖：

- `openssl`
- `curl`
- `nlohmann-json`

`qqbot_cpp` 在 Windows + OpenSSL 环境下如果遇到 TLS 握手问题，可以通过配置 `skipTlsVerify` 临时排查，但不建议在生产环境长期启用。

## 多 agent / 多 channel instance 配置

当前推荐的运行模型是：

- 一个 `channel instance` 表示一个实际接收消息的账号或机器人实例
- 一个 `channel instance` 通过 `binding.agent` 绑定一个 agent
- 一个 `agent` 可以配置独立 `workspace`
- 一个 `agent` 可以通过 `toolProfile` 配置可用工具范围

示例：

```json
{
  "agents": {
    "defaults": {
      "model": "anthropic/claude-opus-4-5",
      "workspace": "D:/kabot/workspaces/default",
      "toolProfile": "full"
    },
    "instances": [
      {
        "name": "ops-agent",
        "workspace": "D:/kabot/workspaces/ops",
        "toolProfile": "full"
      },
      {
        "name": "sales-agent",
        "workspace": "D:/kabot/workspaces/sales",
        "toolProfile": "message_only"
      }
    ]
  },
  "channels": {
    "instances": [
      {
        "name": "telegram_ops",
        "type": "telegram",
        "enabled": true,
        "token": "<telegram-bot-token>",
        "allowFrom": ["12345678"],
        "binding": {
          "agent": "ops-agent"
        }
      },
      {
        "name": "lark_sales",
        "type": "lark",
        "enabled": true,
        "appId": "<lark-app-id>",
        "appSecret": "<lark-app-secret>",
        "binding": {
          "agent": "sales-agent"
        }
      },
      {
        "name": "qqbot_ops",
        "type": "qqbot",
        "enabled": true,
        "appId": "<qqbot-app-id>",
        "clientSecret": "<qqbot-client-secret>",
        "sandbox": true,
        "intents": "1107296256",
        "allowFrom": ["group-openid", "user-openid"],
        "binding": {
          "agent": "ops-agent"
        }
      }
    ]
  }
}
```

### agent 工具档位

当前支持两个 `toolProfile`：

- `full`
  - 使用当前默认的完整工具集
- `message_only`
  - 只允许 `message` 工具

这适合把某些 agent 配置成“只能发消息/转发消息”的轻代理，而不允许文件、命令、网页搜索、cron 等其他工具。

### 迁移说明

旧配置仍然兼容：

- `agents.defaults` 会自动映射为默认 agent 实例 `default`
- `channels.telegram` / `channels.lark` 会自动映射为默认 channel instance

推荐逐步迁移到新的 `channels.instances` 结构，并为每个接收账号显式配置：

- `name`
- `type`
- 渠道接入参数
- `binding.agent`

如果 `binding.agent` 缺失，或引用了不存在的 agent，启动会直接失败。

## QQBot 配置说明

当前支持两种 QQBot 配置入口：

- 旧式单实例：`channels.qqbot`
- 新式多实例：`channels.instances[*]` 配合 `type: "qqbot"`

当前 QQBot 配置字段包括：

- `appId`
- `clientSecret`
- `token`
- `sandbox`
- `intents`
- `skipTlsVerify`
- `allowFrom`
- `binding.agent`

其中：

- `appId` 必填
- `clientSecret` 和 `token` 至少提供一个
- 未显式配置 `intents` 时，运行时会使用 `qqbot_cpp` 的消息回复示例默认值

## QQBot 支持范围与限制

当前 QQBot 渠道实现支持：

- 频道消息 `MESSAGE_CREATE` / `AT_MESSAGE_CREATE`
- 私信消息 `DIRECT_MESSAGE_CREATE`
- C2C 消息 `C2C_MESSAGE_CREATE`
- 群消息 `GROUP_AT_MESSAGE_CREATE` / `GROUP_MSG_RECEIVE`
- 纯文本回复发送
- 基于 `reply_to` / `chat_id` 的会话目标回填

当前已知限制：

- 只做了文本消息归一化，附件暂未映射到统一媒体下载链路
- outbound 目前只覆盖文本发送，不包含 QQBot 富媒体上传
- `allowFrom` 目前按 sender/chat 标识字符串精确匹配，不做更复杂的权限模型
