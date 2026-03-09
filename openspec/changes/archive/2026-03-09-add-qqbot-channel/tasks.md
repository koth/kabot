## 1. Configuration and Validation

- [x] 1.1 Add `QQBotConfig` and related fields to `config_schema.hpp`, including QQBot-specific credentials and runtime options.
- [x] 1.2 Extend `config_loader.cpp` to parse QQBot configuration from both `channels.qqbot` and `channels.instances[*]` with `type: "qqbot"`.
- [x] 1.3 Update config normalization and validation so QQBot instances become valid runtime channel instances and missing required credentials are rejected clearly.

## 2. QQBot Channel Runtime

- [x] 2.1 Create `src/channels/qqbot_channel.hpp` and `src/channels/qqbot_channel.cpp` implementing the `ChannelBase` interface with `qqbot_cpp`.
- [x] 2.2 Implement QQBot inbound event handling that normalizes supported text messages into the shared `InboundMessage` contract.
- [x] 2.3 Implement QQBot outbound text sending that selects the correct QQBot API for direct, group, or channel targets using stored metadata.
- [x] 2.4 Add any required in-memory target/message mapping so replies can be routed back to the correct QQBot conversation.

## 3. Runtime Wiring and Build Integration

- [x] 3.1 Update `ChannelManager` to register and manage QQBot channel instances alongside Telegram and Lark.
- [x] 3.2 Update `src/CMakeLists.txt` to include QQBot channel sources and wire the `qqbot_cpp` dependency into the build.
- [x] 3.3 Verify startup and shutdown behavior for enabled QQBot instances, including graceful cleanup of SDK resources.

## 4. Testing and Documentation

- [x] 4.1 Add config tests covering valid QQBot instances, unsupported/missing credential cases, and channel type validation.
- [x] 4.2 Add routing or channel tests covering QQBot inbound normalization and outbound target selection for text replies.
- [x] 4.3 Update `src/README.md` with QQBot dependency setup, example configuration, supported message scope, and known limitations.
