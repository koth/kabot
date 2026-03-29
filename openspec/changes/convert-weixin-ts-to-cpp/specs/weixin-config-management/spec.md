## ADDED Requirements

### Requirement: Configuration schema validation
The system SHALL validate configuration using a schema.

#### Scenario: Validate valid configuration
- **WHEN** validating a configuration with all required fields
- **THEN** the system SHALL accept the configuration
- **AND** return the validated config object

#### Scenario: Reject invalid configuration
- **WHEN** validating a configuration with missing required fields
- **THEN** the system SHALL reject the configuration
- **AND** report validation errors

#### Scenario: Apply default values
- **WHEN** validating a configuration with optional fields missing
- **THEN** the system SHALL apply default values
- **AND** return the complete configuration

### Requirement: Account configuration fields
The system SHALL support the following account configuration fields:

#### Scenario: Define account fields
- **WHEN** defining an account configuration
- **THEN** the system SHALL support:
  - name (optional): Display name for the account
  - enabled (optional): Whether account is enabled (default: true)
  - baseUrl (optional): API base URL (default: https://ilinkai.weixin.qq.com)
  - cdnBaseUrl (optional): CDN base URL (default: https://novac2c.cdn.weixin.qq.com/c2c)
  - routeTag (optional): SKRouteTag header value for routing

### Requirement: Global configuration
The system SHALL support global configuration options.

#### Scenario: Define global config
- **WHEN** loading global configuration
- **THEN** the system SHALL support:
  - logLevel: Logging verbosity level
  - stateDir: Directory for persistent storage
  - defaultTimeout: Default HTTP timeout in seconds

### Requirement: Configuration loading
The system SHALL load configuration from files.

#### Scenario: Load from JSON file
- **WHEN** loading configuration from a JSON file
- **THEN** the system SHALL parse the JSON content
- **AND** validate against schema
- **AND** return the configuration object

#### Scenario: Handle missing file
- **WHEN** loading configuration and file doesn't exist
- **THEN** the system SHALL return default configuration
- **AND** not throw an error

#### Scenario: Handle malformed JSON
- **WHEN** loading configuration with malformed JSON
- **THEN** the system SHALL throw a parse error
- **AND** include details about the syntax error

### Requirement: Configuration saving
The system SHALL save configuration to files.

#### Scenario: Save to JSON file
- **WHEN** saving configuration to a JSON file
- **THEN** the system SHALL serialize to JSON
- **AND** format with indentation for readability
- **AND** write to the specified file

### Requirement: Environment variable override
The system SHALL support environment variable overrides.

#### Scenario: Override with environment variable
- **WHEN** an environment variable matches a config field
- **THEN** the system SHALL use the environment value
- **AND** override the file-based configuration

#### Scenario: Priority of sources
- **WHEN** configuration is defined in multiple sources
- **THEN** the system SHALL apply in order of priority:
  1. Environment variables (highest)
  2. User configuration file
  3. Default values (lowest)
