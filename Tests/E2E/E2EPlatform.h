// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// The OS-specific pieces of the E2E harness (processes, pipes, environment, sockets). Everything
// else in Tests/E2E is portable. Implemented in E2EPlatform.cpp for Windows and POSIX
// (Linux, macOS).

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace e2e
{
namespace platform
{

// Directory separator for building paths.
#if defined(_WIN32)
constexpr char PathSeparator = '\\';
#else
constexpr char PathSeparator = '/';
#endif

uint32_t ProcessId();
void SleepMs(uint32_t milliseconds);
// Empty string if the variable isn't set.
std::string GetEnv(const char* name);
void SetEnv(const char* name, const std::string& value);
std::string ExecutablePath();
bool FileExists(const std::string& path);
int StrICmp(const char* a, const char* b);

// Flushes stdio and exits without running destructors or atexit handlers. Used when
// libHttpClient may still have work in flight (e.g. HCCleanupAsync hung) so static teardown
// can't race it.
[[noreturn]] void ExitImmediately(int code);

// On a fatal signal (POSIX) or unhandled SEH exception (Windows: no-op, use a debugger / WER),
// print `*currentTest` and a backtrace to stderr before dying, so CI logs show where it crashed.
void InstallCrashHandler(const char* const* currentTest);

// A loopback port nothing is listening on (bound to an ephemeral port, then released).
uint16_t UnusedLoopbackPort();

// A child process started from an argument vector (args[0] is searched on PATH). stdin is the
// null device; stderr is shared with this process; stdout is either shared or captured.
class ChildProcess
{
public:
    struct Options
    {
        bool captureStdout{ false };
        // Kill the child when this process exits or this object is destroyed, even if this
        // process is killed without running destructors (Windows: kill-on-close job object).
        // POSIX has no portable equivalent; callers pass the child a parent-pid watchdog instead.
        bool killWithParent{ false };
    };

    ChildProcess();
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    bool Start(const std::vector<std::string>& args, const Options& options, std::string& error);

    // Reads one '\n'-terminated line (without the terminator) from the captured stdout. Returns
    // false on timeout, or if the child exited / closed stdout first.
    bool ReadLine(std::string& line, std::chrono::milliseconds timeout);

    // Waits for the child to exit and returns its exit code, or -1 if it is still running after
    // `timeout` (it is left running).
    int Wait(std::chrono::milliseconds timeout);

    // Kills the child (if still running) and releases everything.
    void Kill();

    bool Running();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace platform
} // namespace e2e
