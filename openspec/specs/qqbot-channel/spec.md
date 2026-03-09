## ADDED Requirements

### Requirement: Support QQBot channel instances in configuration
The system SHALL allow operators to declare QQBot channel instances in configuration using `type: "qqbot"` and QQBot-specific credentials and runtime options.

#### Scenario: Load a QQBot channel instance
- **WHEN** the configuration defines a channel instance with `type: "qqbot"`, a unique instance name, and the required QQBot credentials
- **THEN** the system loads that instance as a valid channel configuration

#### Scenario: Reject a QQBot channel missing required credentials
- **WHEN** a QQBot channel instance omits required authentication fields
- **THEN** startup fails with a validation error that identifies the channel instance and missing QQBot credentials

### Requirement: Start and stop QQBot channel runtimes
The system SHALL create one runtime QQBot channel object per enabled QQBot channel instance and manage its lifecycle with the existing channel manager.

#### Scenario: Start an enabled QQBot channel
- **WHEN** an enabled QQBot channel instance is present during startup
- **THEN** the channel manager starts a QQBot runtime for that instance

#### Scenario: Stop a running QQBot channel
- **WHEN** the process shuts down or channel shutdown is requested
- **THEN** the QQBot runtime stops receiving events and releases its SDK resources cleanly

### Requirement: Convert QQBot inbound events into Kabot inbound messages
The system SHALL normalize supported QQBot inbound message events into the shared inbound message contract used by agent routing.

#### Scenario: Receive a QQBot text message
- **WHEN** a supported QQBot message event carrying text is received
- **THEN** the channel publishes one `InboundMessage` containing the normalized text, the channel instance name, the bound agent metadata, and a stable chat identifier

#### Scenario: Preserve QQBot reply metadata
- **WHEN** a supported QQBot message event is converted into an `InboundMessage`
- **THEN** the channel stores enough QQBot-specific metadata to let later replies target the correct QQBot conversation type and destination

### Requirement: Send outbound text through QQBot
The system SHALL deliver agent outbound text messages through the correct QQBot send API based on the resolved QQBot target metadata for the conversation.

#### Scenario: Reply to a QQBot private or direct conversation
- **WHEN** an agent emits an outbound text message for a QQBot conversation whose metadata resolves to a direct target
- **THEN** the channel sends the text through the QQBot direct-message API for that target

#### Scenario: Reply to a QQBot group or channel conversation
- **WHEN** an agent emits an outbound text message for a QQBot conversation whose metadata resolves to a group or channel target
- **THEN** the channel sends the text through the matching QQBot group or channel API for that target

### Requirement: Respect allow-from filtering for QQBot senders
The system MUST apply the existing allow-from enforcement model to QQBot inbound senders and conversations.

#### Scenario: Block an unauthorized QQBot sender
- **WHEN** a QQBot inbound message arrives from a sender or conversation that is not allowed by the instance configuration
- **THEN** the system ignores the message and does not publish it to the agent bus
