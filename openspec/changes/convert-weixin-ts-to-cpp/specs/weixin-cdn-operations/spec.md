## ADDED Requirements

### Requirement: File upload orchestration
The system SHALL orchestrate file upload to Weixin CDN.

#### Scenario: Upload file to CDN
- **WHEN** uploading a file with specified media type
- **THEN** the system SHALL calculate MD5 hash of the file
- **AND** generate AES key from the hash
- **AND** obtain pre-signed upload URL from API
- **AND** encrypt file content with AES-128-ECB
- **AND** upload encrypted content to CDN
- **AND** return the CDN media reference

### Requirement: CDN HTTP upload
The system SHALL perform HTTP upload to CDN with retry logic.

#### Scenario: Upload with retry on failure
- **WHEN** uploading to CDN and a network error occurs
- **THEN** the system SHALL retry up to 3 times
- **AND** use exponential backoff between retries
- **AND** throw exception if all retries fail

#### Scenario: No retry on 4xx errors
- **WHEN** CDN returns a 4xx error
- **THEN** the system SHALL NOT retry
- **AND** throw exception immediately

### Requirement: AES-128-ECB encryption
The system SHALL implement AES-128-ECB encryption for file content.

#### Scenario: Encrypt file content
- **WHEN** encrypting file content with AES key
- **THEN** the system SHALL use AES-128-ECB mode
- **AND** apply PKCS7 padding
- **AND** return encrypted bytes

#### Scenario: Calculate padded size
- **WHEN** calculating padded size for encryption
- **THEN** the system SHALL round up to AES block size (16 bytes)
- **AND** return the padded size

### Requirement: File download and decryption
The system SHALL download and decrypt files from CDN.

#### Scenario: Download and decrypt image
- **WHEN** downloading an encrypted image from CDN
- **THEN** the system SHALL download the encrypted content
- **AND** parse the AES key from the media reference
- **AND** decrypt content using AES-128-ECB
- **AND** return decrypted bytes

#### Scenario: Handle key format variations
- **WHEN** AES key is provided in different encoding formats
- **THEN** the system SHALL handle both raw and base64-encoded keys
- **AND** correctly decode before decryption

### Requirement: CDN URL construction
The system SHALL construct CDN URLs for media access.

#### Scenario: Build CDN URL
- **WHEN** constructing CDN URL from media reference
- **THEN** the system SHALL combine base URL with encrypt query parameter
- **AND** return the full URL for download
