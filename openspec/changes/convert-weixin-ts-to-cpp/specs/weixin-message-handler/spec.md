## ADDED Requirements

### Requirement: Inbound message processing
The system SHALL process incoming messages from Weixin.

#### Scenario: Process text message
- **WHEN** a text message is received
- **THEN** the system SHALL extract the text content
- **AND** create a context token for the conversation
- **AND** store the message metadata

#### Scenario: Process image message
- **WHEN** an image message is received
- **THEN** the system SHALL download the image from CDN
- **AND** attach the local file path to the message
- **AND** create a context token

#### Scenario: Process voice message
- **WHEN** a voice message is received
- **THEN** the system SHALL download and transcode the audio
- **AND** convert SILK to WAV format
- **AND** attach the transcoded file path

### Requirement: Context token management
The system SHALL manage context tokens for conversation continuity.

#### Scenario: Create context token
- **WHEN** processing a new message
- **THEN** the system SHALL generate a unique context token
- **AND** associate it with the account and user
- **AND** persist to disk for recovery

#### Scenario: Retrieve context token
- **WHEN** sending a reply to a message
- **THEN** the system SHALL retrieve the context token
- **AND** include it in the reply message

#### Scenario: Context token expiration
- **WHEN** a context token is older than 24 hours
- **THEN** the system SHALL consider it expired
- **AND** generate a new token on next interaction

### Requirement: Outbound message sending
The system SHALL send outbound messages to Weixin users.

#### Scenario: Send text message
- **WHEN** sending a text response
- **THEN** the system SHALL format the message
- **AND** include the context token
- **AND** send via the API

#### Scenario: Send media message
- **WHEN** sending an image response
- **THEN** the system SHALL upload the image to CDN
- **AND** construct the media message
- **AND** send via the API

### Requirement: Markdown conversion
The system SHALL convert Markdown to plain text for Weixin compatibility.

#### Scenario: Convert simple Markdown
- **WHEN** converting Markdown with bold and italic
- **THEN** the system SHALL remove Markdown syntax
- **AND** preserve the text content

#### Scenario: Convert links
- **WHEN** converting Markdown with links
- **THEN** the system SHALL format as "text (url)"
- **AND** ensure Weixin compatibility

### Requirement: Slash command handling
The system SHALL handle slash commands from users.

#### Scenario: Handle /echo command
- **WHEN** receiving "/echo <text>" command
- **THEN** the system SHALL reply with the same text
- **AND** not process as a framework command

#### Scenario: Handle /toggle-debug command
- **WHEN** receiving "/toggle-debug" command
- **THEN** the system SHALL toggle debug mode for the account
- **AND** persist the setting
- **AND** reply with current status

### Requirement: Message ID generation
The system SHALL generate unique message IDs.

#### Scenario: Generate unique ID
- **WHEN** generating a message ID
- **THEN** the system SHALL combine timestamp and random bytes
- **AND** ensure uniqueness across the system
