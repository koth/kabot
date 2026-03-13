## ADDED Requirements

### Requirement: Support sandbox command execution on Unix and Windows hosts
The system SHALL execute sandboxed commands on both Unix-like and Windows hosts through the same `SandboxExecutor` interface.

#### Scenario: Execute a command on a Unix-like host
- **WHEN** the runtime is running on a supported Unix-like host and a tool invokes `SandboxExecutor::Run`
- **THEN** the system executes the command and returns the normalized execution result

#### Scenario: Execute a command on a Windows host
- **WHEN** the runtime is running on a supported Windows host and a tool invokes `SandboxExecutor::Run`
- **THEN** the system executes the command and returns the normalized execution result instead of reporting the platform as unsupported

### Requirement: Preserve execution context across platforms
The system MUST apply the requested working directory and capture separated stdout and stderr streams consistently on Unix and Windows hosts.

#### Scenario: Execute within the requested working directory
- **WHEN** a tool invokes `SandboxExecutor::Run` with a non-empty `working_dir`
- **THEN** the command executes with that directory as its current working directory

#### Scenario: Capture stdout and stderr separately
- **WHEN** a sandboxed command writes to both stdout and stderr
- **THEN** the execution result returns stdout in `output` and stderr in `error` without merging them by default

### Requirement: Enforce timeout semantics consistently
The system MUST terminate sandboxed commands that exceed the requested timeout and report a consistent timeout result on both Unix and Windows hosts.

#### Scenario: Terminate a long-running command after timeout
- **WHEN** a sandboxed command runs longer than the provided timeout
- **THEN** the system stops the command, sets `timed_out` to true, and returns a non-success execution result

### Requirement: Apply sandbox block policy before process launch
The system MUST evaluate command block policy before launching a process on any supported host platform.

#### Scenario: Block a dangerous command on Unix
- **WHEN** a requested command matches a blocked policy token on a Unix-like host
- **THEN** the system does not launch the process and returns a blocked execution result

#### Scenario: Block a dangerous command on Windows
- **WHEN** a requested command matches a blocked policy token on a Windows host
- **THEN** the system does not launch the process and returns a blocked execution result

### Requirement: Keep sandbox-dependent tools usable on both host platforms
The system SHALL ensure built-in tools that depend on sandbox command execution remain operational on Unix and Windows hosts by consuming the same normalized execution contract.

#### Scenario: Execute a shell tool command on Windows
- **WHEN** the `shell` tool runs a command on a Windows host
- **THEN** it returns command output or a diagnosable execution failure derived from the sandbox result

#### Scenario: Start a background task on Windows
- **WHEN** the `spawn` tool launches a background task on a Windows host
- **THEN** the task is started through the sandbox executor and its completion is logged using the normalized execution result

#### Scenario: Run an external helper command from another tool on Windows
- **WHEN** a sandbox-dependent built-in tool such as TTS invokes an external helper command on a Windows host
- **THEN** the tool receives the sandbox result and can continue or fail based on the normalized exit status, timeout flag, and stderr content
