#pragma once

#include <chrono>
#include <string>

namespace kabot::sandbox {

struct ExecResult {
    int exit_code = -1;
    bool timed_out = false;
    bool blocked = false;
    std::string output;
    std::string error;
};

class SandboxExecutor {
public:
    static ExecResult Run(const std::string& command,
                          const std::string& working_dir,
                          std::chrono::seconds timeout);
};

}  // namespace kabot::sandbox
