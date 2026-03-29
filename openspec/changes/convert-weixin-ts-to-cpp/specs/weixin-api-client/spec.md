## ADDED Requirements

### Requirement: HTTP client initialization
The system SHALL provide an HTTP client capable of making HTTPS requests to Weixin iLink API endpoints.

#### Scenario: Client creation with base URL
- **WHEN** a client is created with a base URL and authentication token
- **THEN** the client SHALL store the base URL and token for subsequent requests

### Requirement: Request headers
The system SHALL include required headers in all API requests:
- iLink-App-Id from application configuration
- iLink-App-ClientVersion encoded as uint32
- AuthorizationType set to "ilink_bot_token"
- Authorization with Bearer token
- X-WECHAT-UIN as random uint32 in base64
- SKRouteTag when configured
- Content-Type: application/json for POST requests

#### Scenario: GET request headers
- **WHEN** making a GET request to any endpoint
- **THEN** all required headers SHALL be present
- **AND** the Authorization header SHALL contain the Bearer token

#### Scenario: POST request headers
- **WHEN** making a POST request to any endpoint
- **THEN** all required headers SHALL be present
- **AND** Content-Type SHALL be application/json

### Requirement: getUpdates endpoint
The system SHALL implement the getUpdates endpoint for long-polling incoming messages.

#### Scenario: Successful poll with messages
- **WHEN** getUpdates is called with a timeout and buffer
- **THEN** the system SHALL make a GET request to /cgi-bin/ilink/bot/get_updates
- **AND** return the list of messages received
- **AND** return the new buffer value for the next poll

#### Scenario: Poll timeout with no messages
- **WHEN** getUpdates is called and no messages are available within timeout
- **THEN** the system SHALL return an empty message list
- **AND** return the same buffer value

#### Scenario: Session expiration handling
- **WHEN** getUpdates returns error code -14
- **THEN** the system SHALL detect session expiration
- **AND** throw a SessionExpiredException

### Requirement: sendMessage endpoint
The system SHALL implement the sendMessage endpoint for sending text messages.

#### Scenario: Send text message
- **WHEN** sendMessage is called with user ID, context token, and text content
- **THEN** the system SHALL make a POST request to /cgi-bin/ilink/bot/send_message
- **AND** include the context token from the original message
- **AND** return success status

### Requirement: getUploadUrl endpoint
The system SHALL implement the getUploadUrl endpoint for obtaining CDN upload URLs.

#### Scenario: Get upload URL for image
- **WHEN** getUploadUrl is called with media type IMAGE and file size
- **THEN** the system SHALL make a POST request to /cgi-bin/ilink/bot/get_cdn_upload_url
- **AND** return the pre-signed CDN URL and AES key

### Requirement: Error handling
The system SHALL handle API errors with appropriate retry logic.

#### Scenario: Network error with retry
- **WHEN** a network error occurs during API call
- **THEN** the system SHALL retry up to 3 times with exponential backoff
- **AND** throw an exception if all retries fail

#### Scenario: 4xx client error
- **WHEN** a 4xx error is received from API
- **THEN** the system SHALL NOT retry
- **AND** throw an exception immediately

### Requirement: Connection pooling
The system SHALL reuse connections for multiple requests to the same host.

#### Scenario: Multiple requests to same endpoint
- **WHEN** multiple requests are made to the same API endpoint
- **THEN** the system SHALL reuse the TCP connection when possible
- **AND** reduce connection overhead
