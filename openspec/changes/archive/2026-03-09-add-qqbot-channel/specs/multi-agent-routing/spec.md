## MODIFIED Requirements

### Requirement: Support multiple channel instances
The system SHALL support configuring multiple named channel instances, including multiple instances of the same channel type, within a single process. Supported channel types MUST include `telegram`, `lark`, and `qqbot`.

#### Scenario: Start multiple channel instances of the same type
- **WHEN** the configuration defines two Telegram channel instances with different names and credentials
- **THEN** the system starts both instances and keeps them addressable by their configured instance names

#### Scenario: Start multiple channel types together
- **WHEN** the configuration defines Telegram, Lark, and QQBot channel instances at the same time
- **THEN** the system starts all enabled instances in one runtime

### Requirement: Bind a channel instance to exactly one agent
Each channel instance SHALL declare exactly one target agent through `binding.agent`. The system MUST validate that the referenced agent name exists before startup completes.

#### Scenario: Reject unknown agent binding
- **WHEN** a channel instance references an agent name that is not defined in the agent configuration
- **THEN** startup fails with a validation error that identifies the channel instance and unknown agent name

#### Scenario: Reject missing agent binding
- **WHEN** a channel instance configuration omits `binding.agent`
- **THEN** startup fails with a validation error that identifies the channel instance with the missing binding

### Requirement: Route outbound messages by channel instance
The system SHALL send outbound messages through the configured channel instance identified by the outbound routing fields, rather than only by generic channel type.

#### Scenario: Send through a named channel instance
- **WHEN** an outbound message targets a specific configured channel instance name
- **THEN** the channel manager dispatches the message through that exact instance

#### Scenario: Prevent collision between same-type channel instances
- **WHEN** two channel instances share the same channel type but have different configured names
- **THEN** outbound dispatch does not confuse or overwrite the instances
