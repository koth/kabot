## ADDED Requirements

### Requirement: State directory resolution
The system SHALL resolve the state directory for persistent storage.

#### Scenario: Use environment variable
- **WHEN** OPENCLAW_STATE_DIR environment variable is set
- **THEN** the system SHALL use the specified directory
- **AND** create it if it doesn't exist

#### Scenario: Use default directory
- **WHEN** no environment variable is set
- **THEN** the system SHALL use the default directory (~/.openclaw)
- **AND** create it if it doesn't exist

### Requirement: Sync buffer storage
The system SHALL persist sync buffers for each account.

#### Scenario: Save sync buffer
- **WHEN** saving a sync buffer for an account
- **THEN** the system SHALL write to {accountId}.sync.json
- **AND** include the buffer value
- **AND** apply file locking for thread safety

#### Scenario: Load sync buffer
- **WHEN** loading a sync buffer for an account
- **THEN** the system SHALL read from {accountId}.sync.json
- **AND** return the buffer value
- **AND** return empty if file doesn't exist

#### Scenario: Legacy migration
- **WHEN** loading sync buffer and legacy format is detected
- **THEN** the system SHALL migrate to new format
- **AND** preserve the buffer value

### Requirement: Context token storage
The system SHALL persist context tokens for conversation state.

#### Scenario: Save context token
- **WHEN** saving a context token for a user
- **THEN** the system SHALL write to {accountId}.context-tokens.json
- **AND** map user ID to context token
- **AND** include timestamp for expiration tracking

#### Scenario: Load context token
- **WHEN** loading a context token for a user
- **THEN** the system SHALL read from {accountId}.context-tokens.json
- **AND** return the token if found and not expired
- **AND** return empty if expired or not found

#### Scenario: Cleanup expired tokens
- **WHEN** loading context tokens
- **THEN** the system SHALL remove expired entries (older than 24 hours)
- **AND** save the cleaned file

### Requirement: Account storage
The system SHALL store account configurations.

#### Scenario: Save account configuration
- **WHEN** saving an account
- **THEN** the system SHALL write to accounts/{accountId}.json
- **AND** include all account fields
- **AND** set file permissions to 0o600

#### Scenario: Update account index
- **WHEN** adding or removing an account
- **THEN** the system SHALL update accounts/index.json
- **AND** maintain the list of account IDs

### Requirement: Debug mode persistence
The system SHALL persist debug mode settings.

#### Scenario: Save debug mode
- **WHEN** toggling debug mode for an account
- **THEN** the system SHALL write to debug-mode.json
- **AND** map account ID to debug flag

#### Scenario: Load debug mode
- **WHEN** checking debug mode for an account
- **THEN** the system SHALL read from debug-mode.json
- **AND** return the saved value
- **AND** return false if not found

### Requirement: File locking
The system SHALL implement file locking for concurrent access.

#### Scenario: Lock file for writing
- **WHEN** writing to a state file
- **THEN** the system SHALL acquire an exclusive lock
- **AND** block until lock is available
- **AND** release lock after writing

#### Scenario: Lock file for reading
- **WHEN** reading from a state file
- **THEN** the system SHALL acquire a shared lock
- **AND** allow concurrent reads
- **AND** block if exclusive lock is held
