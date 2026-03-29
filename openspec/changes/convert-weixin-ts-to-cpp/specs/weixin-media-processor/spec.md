## ADDED Requirements

### Requirement: Media type detection
The system SHALL detect media types from file content or extension.

#### Scenario: Detect image type
- **WHEN** detecting media type of an image file
- **THEN** the system SHALL identify it as IMAGE type
- **AND** determine the specific format (JPEG, PNG, etc.)

#### Scenario: Detect video type
- **WHEN** detecting media type of a video file
- **THEN** the system SHALL identify it as VIDEO type
- **AND** determine the specific format (MP4, etc.)

#### Scenario: Detect audio type
- **WHEN** detecting media type of an audio file
- **THEN** the system SHALL identify it as VOICE type
- **AND** handle SILK format specifically

### Requirement: Image download and save
The system SHALL download images from CDN and save to storage.

#### Scenario: Download image
- **WHEN** downloading an image from CDN
- **THEN** the system SHALL fetch the encrypted content
- **AND** decrypt it using AES key
- **AND** save to local file system
- **AND** return the file path

### Requirement: SILK audio transcoding
The system SHALL transcode SILK audio to WAV format.

#### Scenario: Transcode voice message
- **WHEN** processing a voice message in SILK format
- **THEN** the system SHALL download the SILK data from CDN
- **AND** decode SILK to PCM audio
- **AND** wrap PCM in WAV container with 24kHz sample rate
- **AND** save the WAV file

#### Scenario: Handle SILK decoder errors
- **WHEN** SILK decoding fails
- **THEN** the system SHALL log the error
- **AND** continue processing without the audio content
- **AND** not crash the application

### Requirement: File attachment handling
The system SHALL handle file attachments of various types.

#### Scenario: Download file attachment
- **WHEN** downloading a file attachment from CDN
- **THEN** the system SHALL fetch and decrypt the content
- **AND** preserve the original filename
- **AND** save to local file system

#### Scenario: Handle large files
- **WHEN** downloading large files (>100MB)
- **THEN** the system SHALL stream the download
- **AND** not load entire file into memory

### Requirement: Video download
The system SHALL download video files from CDN.

#### Scenario: Download video
- **WHEN** downloading a video from CDN
- **THEN** the system SHALL fetch and decrypt the content
- **AND** save to local file system
- **AND** preserve video format

### Requirement: MIME type detection
The system SHALL detect MIME types from file content.

#### Scenario: Detect MIME from content
- **WHEN** detecting MIME type of a file
- **THEN** the system SHALL examine file headers (magic numbers)
- **AND** return the appropriate MIME type
- **AND** fall back to extension-based detection if headers are inconclusive
