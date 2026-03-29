## Context

The WeChat channel plugin currently exists as a TypeScript implementation for the OpenClaw framework in `package/src`. This plugin provides bidirectional communication between OpenClaw AI agents and WeChat users through the Weixin iLink Bot API. The TypeScript implementation uses Node.js runtime and various npm packages for HTTP communication, encryption, and media processing.

The goal is to convert this entire codebase to C++17 and place it in `src/weixin` for integration with C++-based systems.

**Current Architecture (TypeScript):**
- Modular design with clear separation of concerns
- HTTP client with custom headers for Weixin iLink API
- Long-poll monitoring loop for real-time message delivery
- AES-128-ECB encryption for CDN uploads/downloads
- SILK audio transcoding using wasm library
- Persistent storage for sync buffers and context tokens
- File-based account management with QR code login

**Key Dependencies to Replace:**
- HTTP client: Native TypeScript fetch/axios → libcurl or similar
- JSON parsing: Native TypeScript → nlohmann/json
- AES encryption: crypto-js → OpenSSL
- Logging: tslog → spdlog or similar
- QR code: qrcode-terminal → qrencode or similar
- SILK decoding: silk-wasm → libsilk or custom implementation

## Goals / Non-Goals

**Goals:**
- Complete functional parity with TypeScript implementation
- C++17 standard compliance for broad compiler support
- HTTP client capable of long-polling with timeout support
- Thread-safe implementation for concurrent account handling
- Binary-compatible with Weixin iLink API protocol
- Configurable logging with structured output
- Persistent storage with file locking for concurrent access

**Non-Goals:**
- Performance optimization beyond what's naturally achieved with C++
- GUI implementation (CLI-only, similar to TypeScript version)
- Support for WeChat protocols other than iLink Bot API
- Backwards compatibility with Node.js ecosystem
- Dynamic module loading (static linking preferred)

## Decisions

### Decision 1: HTTP Client Library
**Choice**: libcurl with C++ wrapper (cpp-httplib or curlpp)

**Rationale**: libcurl is mature, widely supported, and handles all required features including:
- Custom headers with authentication tokens
- Long-polling with configurable timeouts
- HTTPS with certificate verification
- Upload/download progress callbacks
- Connection reuse for efficiency

**Alternatives considered**:
- Boost.Beast: Good but requires Boost dependency
- Custom socket implementation: Too much complexity for SSL handling
- WinHTTP: Platform-specific, not portable

### Decision 2: JSON Library
**Choice**: nlohmann/json (single-header)

**Rationale**: 
- Single header file, easy integration
- Modern C++ interface similar to TypeScript object manipulation
- Excellent documentation and community support
- No external dependencies

**Alternatives considered**:
- RapidJSON: Faster but more verbose API
- jsoncpp: Good but requires build integration

### Decision 3: Directory Structure
**Choice**: Mirror TypeScript module structure in C++ headers/sources

```
src/weixin/
├── include/weixin/          # Public headers
│   ├── api/
│   ├── auth/
│   ├── cdn/
│   ├── config/
│   ├── media/
│   ├── messaging/
│   ├── monitor/
│   ├── storage/
│   └── util/
├── src/                     # Implementation files
│   ├── api/
│   ├── auth/
│   ├── cdn/
│   ├── config/
│   ├── media/
│   ├── messaging/
│   ├── monitor/
│   ├── storage/
│   └── util/
├── tests/                   # Unit tests
└── CMakeLists.txt
```

**Rationale**: Maintains clear module boundaries and makes migration from TypeScript intuitive.

### Decision 4: Threading Model
**Choice**: One thread per account for monitoring, thread pool for message processing

**Rationale**:
- Each Weixin account requires independent long-polling
- Thread per account simplifies state management
- Thread pool for message processing prevents resource exhaustion
- std::thread and std::mutex provide portable synchronization

**Alternatives considered**:
- Async I/O with select/poll: More complex, less portable
- Single-threaded with coroutines: Requires C++20

### Decision 5: Error Handling
**Choice**: Exceptions for unexpected errors, return codes for expected failures

**Rationale**:
- Network errors and API failures are expected and should be handled gracefully
- Logic errors (null pointers, invalid states) use exceptions
- Aligns with C++ best practices and RAII

### Decision 6: Memory Management
**Choice**: Smart pointers (std::shared_ptr, std::unique_ptr) with RAII

**Rationale**:
- Eliminates manual memory management bugs
- Clear ownership semantics
- Exception-safe resource cleanup

### Decision 7: Configuration Format
**Choice**: JSON files compatible with TypeScript implementation

**Rationale**:
- Eases migration from existing installations
- No need to convert existing user data
- Human-readable and editable

## Risks / Trade-offs

**[Risk] Silent behavioral differences between TypeScript and C++**
→ Mitigation: Comprehensive test suite comparing outputs, integration testing with real Weixin API

**[Risk] SILK audio transcoding complexity**
→ Mitigation: Research existing C/C++ SILK libraries (Skype's SILK SDK is available), consider WASM bridge if native implementation is too complex

**[Risk] Cross-platform file path handling**
→ Mitigation: Use std::filesystem (C++17) for portable path operations

**[Risk] Thread safety in storage operations**
→ Mitigation: File locking using platform-specific APIs (flock on Unix, LockFile on Windows) or use SQLite for atomic operations

**[Risk] Long-poll timeout handling differences**
→ Mitigation: Test timeout behavior thoroughly on all target platforms

**[Trade-off] Code verbosity**
→ TypeScript's dynamic typing allows more concise code. C++ requires explicit types and error handling, resulting in more verbose but safer code.

**[Trade-off] Build complexity**
→ C++ requires compilation and linking. Trade-off is better performance and no runtime dependencies.

## Migration Plan

**Phase 1: Core Infrastructure**
1. Set up CMake build system
2. Implement HTTP client wrapper
3. Implement JSON serialization/deserialization
4. Implement logging framework
5. Implement AES encryption utilities

**Phase 2: API and Authentication**
1. Implement API client with all endpoints
2. Implement account management
3. Implement QR code login flow
4. Test authentication flow end-to-end

**Phase 3: Messaging**
1. Implement message types and parsing
2. Implement inbound message processing
3. Implement outbound message sending
4. Implement media upload/download

**Phase 4: Monitoring and Storage**
1. Implement long-poll monitoring loop
2. Implement persistent storage
3. Implement state management
4. Test full message flow

**Phase 5: Testing and Polish**
1. Unit tests for all modules
2. Integration tests with real API
3. Performance benchmarks
4. Documentation

**Rollback Strategy:**
- Keep TypeScript implementation functional during development
- Feature flags to switch between implementations
- Gradual migration of accounts one at a time

## Open Questions

1. Should we use a dependency manager like Conan or vcpkg for C++ libraries, or git submodules?
2. What's the minimum supported compiler version (GCC 7, Clang 5, MSVC 2017)?
3. Do we need to support Windows, Linux, and macOS from day one, or focus on one platform first?
4. Should we implement the SILK decoder in C++ or provide a bridge to the existing wasm implementation?
5. How should we handle the qrcode-terminal functionality - require external tool or implement in C++?
