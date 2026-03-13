#include "sandbox/sandbox_executor.hpp"

#include <chrono>
#include <cstdlib>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <system_error>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace kabot::sandbox {

namespace {

constexpr int kBlockedExitCode = 126;
constexpr int kTimedOutExitCode = 124;

const std::array<const char*, 13> kBlockedTokens = {
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
    "chown"
};

const std::array<const char*, 2> kBlockedPipes = {
    "curl | sh",
    "wget | sh"
};

bool IsBlockedCommand(const std::string& command) {
    for (const auto* token : kBlockedTokens) {
        if (command.find(token) != std::string::npos) {
            return true;
        }
    }
    for (const auto* token : kBlockedPipes) {
        if (command.find(token) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string BuildTempLogPath(const char* prefix, const char* suffix) {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return (std::filesystem::temp_directory_path() / (std::string(prefix) + stamp + suffix)).string();
}

void ReadFileToString(const std::filesystem::path& path, std::string& target) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    target = stream.str();
}

void CleanupLogFiles(const std::filesystem::path& stdout_path,
                     const std::filesystem::path& stderr_path) {
    std::error_code ec;
    std::filesystem::remove(stdout_path, ec);
    std::filesystem::remove(stderr_path, ec);
}

void PopulateCapturedOutput(const std::filesystem::path& stdout_path,
                            const std::filesystem::path& stderr_path,
                            ExecResult& result) {
    ReadFileToString(stdout_path, result.output);
    ReadFileToString(stderr_path, result.error);
}

} // namespace

ExecResult SandboxExecutor::Run(const std::string& command,
                                const std::string& working_dir,
                                std::chrono::seconds timeout) {
    ExecResult result{};
    if (IsBlockedCommand(command)) {
        result.exit_code = kBlockedExitCode;
        result.blocked = true;
        result.error = "Error: command blocked by policy";
        return result;
    }

    const auto stdout_path = std::filesystem::path(BuildTempLogPath("kabot_stdout_", ".log"));
    const auto stderr_path = std::filesystem::path(BuildTempLogPath("kabot_stderr_", ".log"));

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE stdout_handle = ::CreateFileA(
        stdout_path.string().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (stdout_handle == INVALID_HANDLE_VALUE) {
        result.exit_code = -1;
        result.error = "Error: failed to open stdout log";
        return result;
    }

    HANDLE stderr_handle = ::CreateFileA(
        stderr_path.string().c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (stderr_handle == INVALID_HANDLE_VALUE) {
        ::CloseHandle(stdout_handle);
        CleanupLogFiles(stdout_path, stderr_path);
        result.exit_code = -1;
        result.error = "Error: failed to open stderr log";
        return result;
    }

    STARTUPINFOA startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_handle;
    startup_info.hStdError = stderr_handle;

    PROCESS_INFORMATION process_info{};
    std::string command_line = std::string("cmd.exe /C ") + command;
    std::vector<char> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back('\0');

    const BOOL created = ::CreateProcessA(
        nullptr,
        command_buffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        working_dir.empty() ? nullptr : working_dir.c_str(),
        &startup_info,
        &process_info);

    ::CloseHandle(stdout_handle);
    ::CloseHandle(stderr_handle);

    if (!created) {
        CleanupLogFiles(stdout_path, stderr_path);
        result.exit_code = -1;
        result.error = "Error: exec failed";
        return result;
    }

    const auto timeout_ms = timeout.count() <= 0
        ? 0UL
        : static_cast<unsigned long>(timeout.count() * 1000ULL);
    const DWORD wait_result = ::WaitForSingleObject(process_info.hProcess, timeout_ms);
    if (wait_result == WAIT_TIMEOUT) {
        result.timed_out = true;
        ::TerminateProcess(process_info.hProcess, kTimedOutExitCode);
        ::WaitForSingleObject(process_info.hProcess, 2000UL);
    }

    DWORD exit_code = 0;
    if (::GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        result.exit_code = static_cast<int>(exit_code);
    }
    if (result.timed_out && result.exit_code == STILL_ACTIVE) {
        result.exit_code = kTimedOutExitCode;
    }

    ::CloseHandle(process_info.hThread);
    ::CloseHandle(process_info.hProcess);

    PopulateCapturedOutput(stdout_path, stderr_path, result);
    CleanupLogFiles(stdout_path, stderr_path);
    if (result.timed_out && result.exit_code == 0) {
        result.exit_code = kTimedOutExitCode;
    }
    return result;
#else
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

    const int stdout_fd = ::open(stdout_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (stdout_fd < 0) {
        result.exit_code = -1;
        result.error = std::string("Error: failed to open stdout log: ") + std::strerror(errno);
        return result;
    }

    const int stderr_fd = ::open(stderr_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (stderr_fd < 0) {
        const int saved_errno = errno;
        ::close(stdout_fd);
        std::error_code ec;
        std::filesystem::remove(stdout_path, ec);
        result.exit_code = -1;
        result.error = std::string("Error: failed to open stderr log: ") + std::strerror(saved_errno);
        return result;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved_errno = errno;
        ::close(stdout_fd);
        ::close(stderr_fd);
        std::error_code ec;
        std::filesystem::remove(stdout_path, ec);
        std::filesystem::remove(stderr_path, ec);
        result.exit_code = -1;
        result.error = std::string("Error: exec failed: ") + std::strerror(saved_errno);
        return result;
    }

    if (pid == 0) {
        if (!working_dir.empty() && ::chdir(working_dir.c_str()) != 0) {
            const auto message = std::string("Error: failed to change directory: ") + std::strerror(errno) + "\n";
            ::write(stderr_fd, message.c_str(), message.size());
            _exit(125);
        }

        for (const auto* key : kProxyVars) {
            if (const char* value = std::getenv(key)) {
                ::setenv(key, value, 1);
            }
        }

        if (::dup2(stdout_fd, STDOUT_FILENO) < 0 || ::dup2(stderr_fd, STDERR_FILENO) < 0) {
            const auto message = std::string("Error: failed to redirect output: ") + std::strerror(errno) + "\n";
            ::write(stderr_fd, message.c_str(), message.size());
            _exit(125);
        }

        ::close(stdout_fd);
        ::close(stderr_fd);

        ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));

        const auto message = std::string("Error: exec failed: ") + std::strerror(errno) + "\n";
        ::write(STDERR_FILENO, message.c_str(), message.size());
        _exit(127);
    }

    ::close(stdout_fd);
    ::close(stderr_fd);

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool finished = false;
    int status = 0;
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
            ::waitpid(pid, &status, 0);
            finished = true;
        }
    }

    if (finished) {
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exit_code = 128 + WTERMSIG(status);
        }
    } else if (result.exit_code == -1) {
        result.exit_code = kTimedOutExitCode;
    }

    PopulateCapturedOutput(stdout_path, stderr_path, result);
    CleanupLogFiles(stdout_path, stderr_path);
    if (result.timed_out && result.exit_code == 0) {
        result.exit_code = kTimedOutExitCode;
    }
    return result;
#endif
}

}  // namespace kabot::sandbox
