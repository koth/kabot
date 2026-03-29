## ADDED Requirements

### Requirement: QR code generation
The system SHALL implement QR code generation for Weixin login.

#### Scenario: Generate login QR code
- **WHEN** requesting a login QR code
- **THEN** the system SHALL make a POST request to /cgi-bin/ilink/bot/get_bot_qrcode
- **AND** receive a QR code URL and expiration timestamp
- **AND** return the QR code data for display

### Requirement: QR code display
The system SHALL support displaying QR codes in terminal.

#### Scenario: Display QR in terminal
- **WHEN** a QR code URL is received
- **THEN** the system SHALL download the QR code image
- **AND** render it as ASCII art in the terminal
- **OR** provide a command to display it using external tools

### Requirement: QR status polling
The system SHALL poll for QR code scan status.

#### Scenario: Poll for successful scan
- **WHEN** polling for QR status with a token
- **THEN** the system SHALL make requests to /cgi-bin/ilink/bot/get_qrcode_status
- **AND** return the current status (pending, scanned, confirmed, expired)
- **AND** return the authentication token on successful confirmation

#### Scenario: Auto-refresh on expiration
- **WHEN** a QR code expires and auto-refresh is enabled
- **THEN** the system SHALL automatically generate a new QR code
- **AND** continue polling up to the maximum retry count (3)

### Requirement: IDC redirect handling
The system SHALL handle IDC (Internet Data Center) redirects during login.

#### Scenario: Handle redirect status
- **WHEN** QR status returns "scaned_but_redirect"
- **THEN** the system SHALL extract the redirect URL
- **AND** retry QR generation with the new IDC endpoint

### Requirement: Token extraction
The system SHALL extract and store the authentication token on successful login.

#### Scenario: Extract token from confirmation
- **WHEN** QR status returns confirmed with bot_info
- **THEN** the system SHALL extract the authentication token
- **AND** store it in the account configuration
- **AND** return the account object

### Requirement: User pairing authorization
The system SHALL manage user pairing for command authorization.

#### Scenario: Check user authorization
- **WHEN** checking if a user is authorized to send commands
- **THEN** the system SHALL check the allow-list for the user ID
- **AND** return true if authorized, false otherwise

#### Scenario: Authorize user
- **WHEN** authorizing a new user
- **THEN** the system SHALL add the user ID to the allow-list
- **AND** persist the change to disk with file locking
