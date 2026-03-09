## ADDED Requirements

### Requirement: Support multiple named agent instances
The system SHALL support configuring multiple named agent instances. Each agent instance MUST have a stable unique name and MAY override default agent settings, including workspace and model-related parameters.

#### Scenario: Load multiple configured agents
- **WHEN** the configuration defines more than one agent instance
- **THEN** the system creates a distinct runtime agent instance for each configured agent name

#### Scenario: Apply default values to agent instances
- **WHEN** an agent instance omits optional settings
- **THEN** the system inherits those settings from the global agent defaults

#### Scenario: Override workspace per agent
- **WHEN** two configured agent instances specify different workspaces
- **THEN** each agent uses its own configured workspace without affecting the other agent

### Requirement: Restrict tools per agent through tool profiles
The system SHALL allow each configured agent instance to declare a supported tool profile. The first supported profiles are `full` and `message_only`.

#### Scenario: Register the full tool set
- **WHEN** an agent instance uses `toolProfile: full`
- **THEN** the runtime registers the default complete tool set for that agent

#### Scenario: Register only the message tool
- **WHEN** an agent instance uses `toolProfile: message_only`
- **THEN** the runtime registers only the `message` tool for that agent

#### Scenario: Reject an unsupported tool profile
- **WHEN** an agent instance uses any `toolProfile` value other than `full` or `message_only`
- **THEN** startup fails with a validation error that identifies the agent instance and invalid tool profile

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

### Requirement: Route inbound messages to the correct agent
The system SHALL route every inbound message to exactly one target agent based on the configured `binding.agent` for the receiving channel instance and any explicit valid agent metadata attached to the message.

#### Scenario: Route to the bound agent
- **WHEN** an inbound message arrives on a channel instance that declares `binding.agent`
- **THEN** the message is routed to that bound agent

#### Scenario: Respect explicit valid agent metadata
- **WHEN** an inbound message carries a valid `agent_name` that matches a configured agent
- **THEN** the system routes the message to that agent

### Requirement: Isolate session state by agent identity
The system MUST isolate conversation state by agent identity so that two agents do not share the same session history unless explicitly configured to do so.

#### Scenario: Keep histories separate for different agents
- **WHEN** two different channel instances bind different agents and receive messages from the same user or chat identifier
- **THEN** each agent maintains an independent session history

#### Scenario: Build session key with channel instance and agent name
- **WHEN** the system creates or looks up session state for an inbound message
- **THEN** the lookup key includes both the channel instance identity and the resolved agent name

### Requirement: Route outbound messages by channel instance
The system SHALL send outbound messages through the configured channel instance identified by the outbound routing fields, rather than only by generic channel type.

#### Scenario: Send through a named channel instance
- **WHEN** an outbound message targets a specific configured channel instance name
- **THEN** the channel manager dispatches the message through that exact instance

#### Scenario: Prevent collision between same-type channel instances
- **WHEN** two channel instances share the same channel type but have different configured names
- **THEN** outbound dispatch does not confuse or overwrite the instances

### Requirement: Preserve backward compatibility for single-instance configurations
The system SHALL support existing single-agent and fixed-channel configuration formats by translating them into the new internal multi-instance model.

#### Scenario: Load legacy single-agent configuration
- **WHEN** the configuration only defines legacy `agents.defaults` values without explicit agent instances
- **THEN** the system creates an internal default agent instance compatible with prior behavior

#### Scenario: Load legacy channel configuration
- **WHEN** the configuration uses legacy `channels.telegram` or `channels.lark` sections
- **THEN** the system maps each enabled legacy section to a default named channel instance with equivalent behavior
