#include "sandbox/sandbox_executor.hpp"

#include <boost/process/v1.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace kabot::sandbox {
namespace bp = boost::process::v1;

ExecResult SandboxExecutor::Run(const std::string& command,
                                const std::string& working_dir,
                                std::chrono::seconds timeout) {
    ExecResult result{};
    static const std::vector<std::string> kBlockedTokens = {
        "rm -rf",
        "rm -r",
        "shutdown",
        "reboot",
        "mkfs",
        "dd ",
        ":(){:|:&};:",
        "sudo ",
        "su ",
        "kill -9",
        "killall",
        "chmod 777",
        "chown",
        "curl | sh",
        "wget | sh"
    };
    for (const auto& token : kBlockedTokens) {
        if (command.find(token) != std::string::npos) {
            result.blocked = true;
            result.output = "Error: command blocked by policy";
            return result;
        }
    }
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto stdout_path = std::filesystem::temp_directory_path() / ("kabot_stdout_" + stamp + ".log");
    const auto stderr_path = std::filesystem::temp_directory_path() / ("kabot_stderr_" + stamp + ".log");

    bp::environment env = boost::this_process::environment();
    const char* kProxyVars[] = {
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "ALL_PROXY",
        "http_proxy",
        "https_proxy",
        "all_proxy",
        "NO_PROXY",
        "no_proxy"
    };
    for (const auto* key : kProxyVars) {
        if (const char* value = std::getenv(key)) {
            env[key] = value;
        }
    }

    try {
        bp::child child_process(
            "/bin/sh",
            "-c",
            command,
            env,
            bp::start_dir=working_dir,
            bp::std_out > stdout_path,
            bp::std_err > stderr_path);

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        bool finished = false;
        int status = 0;
        const pid_t pid = child_process.id();
        while (std::chrono::steady_clock::now() < deadline) {
            const auto waited = ::waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                finished = true;
                break;
            }
            if (waited < 0) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (!finished) {
            result.timed_out = true;
            ::kill(pid, SIGTERM);
            const auto grace_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < grace_deadline) {
                const auto waited = ::waitpid(pid, &status, WNOHANG);
                if (waited == pid) {
                    finished = true;
                    break;
                }
                if (waited < 0) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!finished) {
                ::kill(pid, SIGKILL);
                ::waitpid(pid, &status, WNOHANG);
            }
        }

        if (finished) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exit_code = 128 + WTERMSIG(status);
            }
        } else if (result.exit_code == -1) {
            result.exit_code = 124;
        }
    } catch (const boost::process::v1::process_error& ex) {
        result.exit_code = -1;
        result.output = std::string("Error: exec failed: ") + ex.what();
    }

    std::ostringstream output_stream;
    std::ostringstream error_stream;
    auto read_file = [&](const std::filesystem::path& path, std::ostringstream& target) {
        std::ifstream input(path);
        if (!input.is_open()) {
            return;
        }
        target << input.rdbuf();
    };
    read_file(stdout_path, output_stream);
    read_file(stderr_path, error_stream);
    result.output = output_stream.str();
    result.error = error_stream.str();

    std::error_code ec;
    std::filesystem::remove(stdout_path, ec);
    std::filesystem::remove(stderr_path, ec);
    return result;
}

}  // namespace kabot::sandbox
