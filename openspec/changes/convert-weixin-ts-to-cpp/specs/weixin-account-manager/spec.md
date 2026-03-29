## ADDED Requirements

### Requirement: Account storage format
The system SHALL store account information in JSON files with the following structure:
- Account ID (normalized format)
- Authentication token
- Base URL for API
- CDN base URL
- Route tag for SKRouteTag header
- Display name
- Enabled flag

#### Scenario: Load account from file
- **WHEN** loading an account from a JSON file
- **THEN** the system SHALL parse all account fields
- **AND** validate required fields (accountId, token)

#### Scenario: Save account to file
- **WHEN** saving an account to a JSON file
- **THEN** the system SHALL write all account fields
- **AND** set file permissions to 0o600 for security

### Requirement: Account ID normalization
The system SHALL normalize account IDs by replacing special characters.

#### Scenario: Normalize account ID with special characters
- **WHEN** an account ID like "user@domain.com" is provided
- **THEN** the system SHALL normalize it to "user-domain-com"
- **AND** remove any characters that are not alphanumeric, hyphen, or underscore

### Requirement: Account listing
The system SHALL provide a method to list all configured accounts.

#### Scenario: List all accounts
- **WHEN** requesting a list of all accounts
- **THEN** the system SHALL return all accounts from storage
- **AND** include account metadata (name, enabled status)

### Requirement: Account resolution
The system SHALL resolve account by ID or index.

#### Scenario: Resolve by account ID
- **WHEN** resolving an account by its normalized ID
- **THEN** the system SHALL return the matching account
- **AND** throw AccountNotFoundException if not found

#### Scenario: Resolve by index
- **WHEN** resolving an account by numeric index
- **THEN** the system SHALL return the account at that position
- **AND** throw InvalidIndexException if out of range

### Requirement: Multi-account support
The system SHALL support multiple accounts with an index file.

#### Scenario: Load accounts from index
- **WHEN** loading accounts from an index file
- **THEN** the system SHALL load all accounts listed in the index
- **AND** maintain the order specified in the index

### Requirement: Legacy token migration
The system SHALL support migration from legacy token format.

#### Scenario: Migrate legacy token
- **WHEN** a legacy token file is detected
- **THEN** the system SHALL migrate it to the new account format
- **AND** preserve the original token value
