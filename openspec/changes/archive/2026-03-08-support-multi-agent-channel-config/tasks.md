## 1. Configuration Model

- [x] 1.1 Extend `config_schema.hpp` to represent multiple named agent instances and multiple named channel instances while preserving default agent settings.
- [x] 1.2 Update `config_loader.cpp` to parse the new multi-instance JSON configuration format for agents and channels.
- [x] 1.3 Add compatibility mapping so legacy single-agent and fixed-channel config sections normalize into the new internal runtime model.
- [x] 1.4 Add startup validation for duplicate names, missing channel agent bindings, and references to unknown agents.

## 2. Agent Runtime Management

- [x] 2.1 Introduce a runtime registry or manager that creates and stores one `AgentLoop` instance per configured agent.
- [x] 2.2 Ensure each agent instance receives its own workspace, session manager context, memory state, and tool configuration.
- [x] 2.3 Update startup wiring in `commands.cpp` so the process initializes all configured agent instances instead of a single default agent.
- [x] 2.4 Add per-agent `toolProfile` support so agents can run either the full tool set or `message_only`.

## 3. Channel Instance Management and Routing

- [x] 3.1 Refactor `ChannelManager` to register and address channels by configured instance name rather than by channel type alone.
- [x] 3.2 Update channel construction so multiple instances of the same channel type can run simultaneously.
- [x] 3.3 Extend inbound message metadata and routing logic to resolve a target agent directly from `binding.agent` and any explicit valid `agent_name` metadata.
- [x] 3.4 Update outbound dispatch so messages target a specific channel instance and cannot collide across same-type instances.

## 4. Session Isolation and Message Contracts

- [x] 4.1 Update session key generation so it includes both channel instance identity and resolved agent name.
- [x] 4.2 Add or update bus message fields needed to preserve channel-instance and agent routing information end to end.
- [x] 4.3 Ensure cron, heartbeat, and direct processing paths select the intended agent and channel instance under the new model.

## 5. Compatibility, Testing, and Documentation

- [x] 5.1 Add tests for multi-agent config loading, legacy config compatibility, and startup validation failures.
- [x] 5.2 Add routing tests for single-agent channel bindings, explicit valid agent metadata overrides, and session isolation behavior.
- [x] 5.3 Update README and example configuration to document multi-agent, multi-channel-instance setup and per-agent workspace behavior.
- [x] 5.4 Document migration guidance from legacy `agents.defaults` and fixed `channels.telegram` / `channels.lark` configuration to the new format.
