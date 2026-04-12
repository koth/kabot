#include "agent/tools/shell.hpp"
#include "sandbox/sandbox_executor.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[sandbox_tests] " << message << std::endl;
        std::exit(1);
    }
}

std::string SuccessCommand(const std::string& text) {
#ifdef _WIN32
    return "echo " + text;
#else
    return "printf '" + text + "'";
#endif
}

std::string StdoutStderrCommand() {
#ifdef _WIN32
    return "cmd.exe /C \"echo out&& echo err 1>&2\"";
#else
    return "sh -c 'printf out; printf err 1>&2'";
#endif
}

std::string WorkingDirCommand(const std::filesystem::path& marker) {
#ifdef _WIN32
    return "cmd.exe /C \"echo ok> \"" + marker.string() + "\"\"";
#else
    return "sh -c 'printf ok > \"" + marker.string() + "\"'";
#endif
}

std::string SleepCommand() {
#ifdef _WIN32
    return "ping 127.0.0.1 -n 6 > nul";
#else
    return "sleep 5";
#endif
}

void TestExecuteCommand() {
    const auto result = kabot::sandbox::SandboxExecutor::Run(
        SuccessCommand("sandbox-ok"),
        std::filesystem::current_path().string(),
        std::chrono::seconds(5));
    Expect(!result.blocked, "expected command not to be blocked");
    Expect(!result.timed_out, "expected command not to time out");
    Expect(result.exit_code == 0, "expected command exit code 0");
    Expect(result.output.find("sandbox-ok") != std::string::npos, "expected stdout to contain sandbox-ok");
}

void TestCaptureStdoutAndStderr() {
    const auto result = kabot::sandbox::SandboxExecutor::Run(
        StdoutStderrCommand(),
        std::filesystem::current_path().string(),
        std::chrono::seconds(5));
    Expect(result.exit_code == 0, "expected stdout/stderr command exit code 0");
    Expect(result.output.find("out") != std::string::npos, "expected stdout capture");
    Expect(result.error.find("err") != std::string::npos, "expected stderr capture");
}

void TestWorkingDirectory() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "kabot_sandbox_tests_workdir";
    std::filesystem::create_directories(temp_dir);
    const auto marker = temp_dir / "marker.txt";
    std::error_code ec;
    std::filesystem::remove(marker, ec);

    const auto result = kabot::sandbox::SandboxExecutor::Run(
        WorkingDirCommand(std::filesystem::path("marker.txt")),
        temp_dir.string(),
        std::chrono::seconds(5));
    Expect(result.exit_code == 0, "expected working directory command exit code 0");
    Expect(std::filesystem::exists(marker), "expected marker file in working directory");
}

void TestBlockedCommand() {
    const auto result = kabot::sandbox::SandboxExecutor::Run(
        "rm -rf /tmp/test",
        std::filesystem::current_path().string(),
        std::chrono::seconds(5));
    Expect(result.blocked, "expected dangerous command to be blocked");
    Expect(result.exit_code != 0, "expected blocked command to return non-zero exit code");
}

void TestTimeout() {
    const auto result = kabot::sandbox::SandboxExecutor::Run(
        SleepCommand(),
        std::filesystem::current_path().string(),
        std::chrono::seconds(1));
    Expect(result.timed_out, "expected long-running command to time out");
    Expect(result.exit_code != 0, "expected timed out command to return non-zero exit code");
}

void TestShellToolBlocked() {
    kabot::agent::tools::BashTool tool(std::filesystem::current_path().string());
    std::unordered_map<std::string, std::string> params;
    params["command"] = "rm -rf /tmp/test";
    const auto result = tool.Execute(params);
    Expect(result.find("blocked") != std::string::npos, "expected shell tool to surface blocked error");
}

}  // namespace

int main() {
    TestExecuteCommand();
    TestCaptureStdoutAndStderr();
    TestWorkingDirectory();
    TestBlockedCommand();
    TestTimeout();
    TestShellToolBlocked();
    std::cout << "sandbox_tests passed" << std::endl;
    return 0;
}
