## ADDED Requirements

### Requirement: Long-poll monitoring loop
The system SHALL implement a long-polling loop for incoming messages.

#### Scenario: Start monitoring
- **WHEN** starting monitoring for an account
- **THEN** the system SHALL begin long-polling the getUpdates endpoint
- **AND** process received messages
- **AND** continue until stopped

#### Scenario: Handle poll timeout
- **WHEN** a poll request times out with no messages
- **THEN** the system SHALL immediately start a new poll
- **AND** not wait between requests

### Requirement: Consecutive failure handling
The system SHALL handle consecutive failures with backoff.

#### Scenario: Handle intermittent failures
- **WHEN** consecutive failures occur (up to 3)
- **THEN** the system SHALL continue retrying immediately
- **AND** log the failures

#### Scenario: Enter cooldown after max failures
- **WHEN** more than 3 consecutive failures occur
- **THEN** the system SHALL enter a 30-second cooldown
- **AND** retry after the cooldown period

### Requirement: Session expiration detection
The system SHALL detect and handle session expiration.

#### Scenario: Detect session expiration
- **WHEN** getUpdates returns error code -14
- **THEN** the system SHALL detect session expiration
- **AND** pause monitoring for 1 hour
- **AND** log the session expiration

#### Scenario: Resume after session pause
- **WHEN** the 1-hour pause period elapses
- **THEN** the system SHALL resume monitoring
- **AND** attempt to reconnect

### Requirement: Sync buffer persistence
The system SHALL persist the sync buffer for resume capability.

#### Scenario: Save sync buffer
- **WHEN** receiving a new buffer value from getUpdates
- **THEN** the system SHALL save it to persistent storage
- **AND** use it for the next poll request

#### Scenario: Load sync buffer on startup
- **WHEN** starting monitoring
- **THEN** the system SHALL load the last known buffer value
- **AND** use it for the initial poll

### Requirement: Account lifecycle management
The system SHALL manage the lifecycle of monitored accounts.

#### Scenario: Start account monitoring
- **WHEN** starting monitoring for a specific account
- **THEN** the system SHALL create a monitoring thread
- **AND** begin the long-poll loop

#### Scenario: Stop account monitoring
- **WHEN** stopping monitoring for an account
- **THEN** the system SHALL gracefully terminate the monitoring thread
- **AND** save any pending state

#### Scenario: Handle multiple accounts
- **WHEN** monitoring multiple accounts
- **THEN** the system SHALL create separate threads for each
- **AND** isolate failures between accounts
