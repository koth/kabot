## 1. Project Setup and Infrastructure

- [ ] 1.1 Create directory structure: src/weixin with include/, src/, tests/ subdirectories
- [ ] 1.2 Set up CMakeLists.txt with C++17 requirement and library targets
- [ ] 1.3 Add nlohmann/json as header-only dependency (single header)
- [ ] 1.4 Set up libcurl linking (find_package or FetchContent)
- [ ] 1.5 Set up OpenSSL for AES encryption
- [ ] 1.6 Add spdlog or similar logging library
- [ ] 1.7 Create initial header structure matching TypeScript module organization
- [ ] 1.8 Set up test framework (Google Test or Catch2)

## 2. Utility Modules

- [ ] 2.1 Implement logger utility (src/weixin/util/logger.h/cpp)
- [ ] 2.2 Implement sensitive data redaction utility (src/weixin/util/redact.h/cpp)
- [ ] 2.3 Implement random ID generation utility (src/weixin/util/random.h/cpp)
- [ ] 2.4 Implement AES-128-ECB encryption/decryption (src/weixin/cdn/aes-ecb.h/cpp)
- [ ] 2.5 Implement PKCS7 padding utilities
- [ ] 2.6 Write unit tests for utility functions

## 3. Configuration Management

- [ ] 3.1 Define account configuration structures (src/weixin/config/account.h)
- [ ] 3.2 Implement JSON schema validation for config (src/weixin/config/config-schema.h/cpp)
- [ ] 3.3 Implement configuration loading from JSON files
- [ ] 3.4 Implement environment variable override support
- [ ] 3.5 Implement default value handling
- [ ] 3.6 Write unit tests for configuration management

## 4. HTTP API Client

- [ ] 4.1 Implement HTTP client wrapper class (src/weixin/api/api.h/cpp)
- [ ] 4.2 Implement request header generation with authentication
- [ ] 4.3 Implement getUpdates endpoint with long-polling support
- [ ] 4.4 Implement sendMessage endpoint for text messages
- [ ] 4.5 Implement getUploadUrl endpoint for CDN operations
- [ ] 4.6 Implement error handling and retry logic (3 retries with backoff)
- [ ] 4.7 Implement session expiration detection (error code -14)
- [ ] 4.8 Implement connection pooling
- [ ] 4.9 Define API types and structures (src/weixin/api/types.h)
- [ ] 4.10 Write unit tests for API client (mock HTTP responses)

## 5. State Storage

- [ ] 5.1 Implement state directory resolution (src/weixin/storage/state-dir.h/cpp)
- [ ] 5.2 Implement sync buffer storage (src/weixin/storage/sync-buf.h/cpp)
- [ ] 5.3 Implement context token storage with expiration
- [ ] 5.4 Implement account storage with file locking
- [ ] 5.5 Implement debug mode persistence
- [ ] 5.6 Implement legacy format migration support
- [ ] 5.7 Write unit tests for storage operations

## 6. Account Management

- [ ] 6.1 Implement account ID normalization (src/weixin/auth/accounts.h/cpp)
- [ ] 6.2 Implement account loading from storage
- [ ] 6.3 Implement account saving with secure permissions
- [ ] 6.4 Implement account listing and resolution
- [ ] 6.5 Implement multi-account index management
- [ ] 6.6 Implement legacy token migration
- [ ] 6.7 Write unit tests for account management

## 7. QR Code Login

- [ ] 7.1 Implement QR code generation endpoint (src/weixin/auth/login-qr.h/cpp)
- [7.2] 7.2 Implement QR code status polling
- [ ] 7.3 Implement IDC redirect handling for scaned_but_redirect status
- [ ] 7.4 Implement auto-refresh on QR expiration (max 3 retries)
- [ ] 7.5 Implement token extraction from confirmed login
- [ ] 7.6 Implement QR code display in terminal (ASCII or external command)
- [ ] 7.7 Write unit tests for QR login flow

## 8. User Pairing

- [ ] 8.1 Implement user pairing storage (src/weixin/auth/pairing.h/cpp)
- [ ] 8.2 Implement allow-list checking for authorization
- [ ] 8.3 Implement user authorization (add to allow-list)
- [ ] 8.4 Implement file locking for concurrent access
- [ ] 8.5 Write unit tests for pairing operations

## 9. CDN Operations

- [ ] 9.1 Implement CDN URL construction (src/weixin/cdn/cdn-url.h/cpp)
- [ ] 9.2 Implement file upload orchestration (src/weixin/cdn/upload.h/cpp)
- [ ] 9.3 Implement MD5 hash calculation for files
- [ ] 9.4 Implement AES key generation from MD5
- [ ] 9.5 Implement CDN HTTP upload with retry logic
- [ ] 9.6 Implement file download and decryption (src/weixin/cdn/pic-decrypt.h/cpp)
- [ ] 9.7 Handle AES key format variations
- [ ] 9.8 Write unit tests for CDN operations

## 10. Media Processing

- [ ] 10.1 Implement MIME type detection from file content (src/weixin/media/mime.h/cpp)
- [ ] 10.2 Implement media type detection (image, video, voice, file)
- [ ] 10.3 Implement image download and save (src/weixin/media/media-download.h/cpp)
- [ ] 10.4 Implement file attachment download
- [ ] 10.5 Implement video download
- [ ] 10.6 Research SILK decoder options (libsilk or custom)
- [ ] 10.7 Implement SILK audio transcoding to WAV (src/weixin/media/silk-transcode.h/cpp)
- [ ] 10.8 Implement PCM to WAV container wrapping with 24kHz sample rate
- [ ] 10.9 Implement streaming download for large files
- [ ] 10.10 Write unit tests for media processing

## 11. Message Handling

- [ ] 11.1 Define message type structures (src/weixin/api/types.h extensions)
- [ ] 11.2 Implement inbound message processing (src/weixin/messaging/inbound.h/cpp)
- [ ] 11.3 Implement context token creation and retrieval
- [ ] 11.4 Implement message ID generation (timestamp + random)
- [ ] 11.5 Implement outbound text message sending (src/weixin/messaging/send.h/cpp)
- [ ] 11.6 Implement outbound media message sending (src/weixin/messaging/send-media.h/cpp)
- [ ] 11.7 Implement Markdown to plain text conversion
- [ ] 11.8 Implement slash command handling (/echo, /toggle-debug)
- [ ] 11.9 Implement message processing pipeline (src/weixin/messaging/process-message.h/cpp)
- [ ] 11.10 Write unit tests for message handling

## 12. Monitoring

- [ ] 12.1 Implement long-poll monitoring loop (src/weixin/monitor/monitor.h/cpp)
- [ ] 12.2 Implement consecutive failure handling with backoff
- [ ] 12.3 Implement session expiration detection and 1-hour pause
- [ ] 12.4 Implement sync buffer persistence during monitoring
- [ ] 12.5 Implement account lifecycle management (start/stop per account)
- [ ] 12.6 Implement thread-per-account model
- [ ] 12.7 Implement graceful shutdown
- [ ] 12.8 Write unit tests for monitoring logic

## 13. Integration and Testing

- [ ] 13.1 Create main entry point (src/weixin/main.cpp)
- [ ] 13.2 Implement CLI argument parsing
- [ ] 13.3 Integrate all modules into cohesive application
- [ ] 13.4 Create integration tests with mock Weixin API
- [ ] 13.5 Test authentication flow end-to-end
- [ ] 13.6 Test message send/receive flow
- [ ] 13.7 Test media upload/download
- [ ] 13.8 Test error handling and recovery
- [ ] 13.9 Create example configuration files

## 14. Documentation and Polish

- [ ] 14.1 Write README.md with build instructions
- [ ] 14.2 Document configuration options
- [ ] 14.3 Create API documentation (Doxygen or similar)
- [ ] 14.4 Add usage examples
- [ ] 14.5 Document migration path from TypeScript
- [ ] 14.6 Add error handling documentation
- [ ] 14.7 Create troubleshooting guide
- [ ] 14.8 Review and update all documentation

## 15. Performance and Optimization

- [ ] 15.1 Profile memory usage
- [ ] 15.2 Optimize hot paths
- [ ] 15.3 Ensure thread safety in all modules
- [ ] 15.4 Add memory leak detection (valgrind or similar)
- [ ] 15.5 Benchmark against TypeScript implementation
- [ ] 15.6 Optimize file I/O operations
