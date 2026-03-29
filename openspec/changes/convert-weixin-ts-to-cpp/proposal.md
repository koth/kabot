## Why

The current WeChat (Weixin) channel plugin is implemented in TypeScript for the OpenClaw framework. To integrate this functionality into a C++-based system and improve performance for high-throughput messaging scenarios, we need a native C++ implementation. This conversion will eliminate the Node.js runtime dependency and enable tighter integration with existing C++ infrastructure.

## What Changes

- **BREAKING**: Remove TypeScript/JavaScript runtime dependency - the new implementation requires C++17 or later
- Convert all TypeScript modules to C++ with equivalent functionality:
  - API layer for HTTP communication with Weixin iLink API
  - Authentication and account management with QR code login
  - CDN operations with AES-128-ECB encryption
  - Media processing including SILK audio transcoding
  - Message handling for inbound and outbound messages
  - Long-poll monitoring for real-time message delivery
  - Persistent storage for state management
- Maintain protocol compatibility with existing Weixin iLink API
- Implement equivalent configuration and logging capabilities
- Create C++ equivalents for all utility functions

## Capabilities

### New Capabilities
- `weixin-api-client`: HTTP client for Weixin iLink API with authentication headers
- `weixin-account-manager`: Account storage, loading, and resolution with multi-account support
- `weixin-qr-login`: QR code generation and polling for user authentication
- `weixin-cdn-operations`: File upload/download with AES-128-ECB encryption
- `weixin-media-processor`: Media download and SILK audio transcoding
- `weixin-message-handler`: Inbound message processing and outbound message sending
- `weixin-monitor`: Long-poll monitoring loop for incoming messages
- `weixin-state-storage`: Persistent storage for sync buffers and context tokens
- `weixin-config-management`: Configuration schema validation and management

### Modified Capabilities
- None - this is a complete reimplementation, not a modification of existing capabilities

## Impact

- New C++ dependencies: HTTP client library (libcurl or similar), JSON parsing (nlohmann/json), AES encryption (OpenSSL), logging framework
- Build system changes: CMake configuration for C++ compilation
- API compatibility: Must maintain exact protocol compatibility with Weixin iLink API
- Testing: Comprehensive test suite needed to ensure behavioral equivalence
- Documentation: C++ API documentation and usage examples
- Migration: Existing TypeScript configurations and state files need migration path
