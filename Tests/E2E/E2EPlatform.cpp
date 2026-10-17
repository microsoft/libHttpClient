// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "E2EPlatform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <execinfo.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <mach-o/dyld.h>
#define E2E_ENVIRON (*_NSGetEnviron())
#else
extern char** environ;
#define E2E_ENVIRON environ
#endif
#endif

namespace e2e
{
namespace platform
{

void SleepMs(uint32_t milliseconds)
{
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    usleep(static_cast<useconds_t>(milliseconds) * 1000);
#endif
}

#if defined(_WIN32)

// ------------------------------------------------------------------------------------------------
// Windows
// ------------------------------------------------------------------------------------------------

uint32_t ProcessId()
{
    return GetCurrentProcessId();
}

std::string GetEnv(const char* name)
{
    char buffer[4096];
    DWORD n = GetEnvironmentVariableA(name, buffer, sizeof(buffer));
    return (n > 0 && n < sizeof(buffer)) ? std::string{ buffer } : std::string{};
}

void SetEnv(const char* name, const std::string& value)
{
    SetEnvironmentVariableA(name, value.c_str());
}

std::string ExecutablePath()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return path;
}

bool FileExists(const std::string& path)
{
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int StrICmp(const char* a, const char* b)
{
    return _stricmp(a, b);
}

void ExitImmediately(int code)
{
    fflush(stdout);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
    _exit(code); // not reached
}

void InstallCrashHandler(const char* const*)
{
}

uint16_t UnusedLoopbackPort()
{
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uint16_t port = 0;
    if (s != INVALID_SOCKET && bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
    {
        int len = sizeof(addr);
        getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
    }
    if (s != INVALID_SOCKET)
    {
        closesocket(s);
    }
    WSACleanup();
    return port;
}

namespace
{
// Quotes one argument for CreateProcess / the MSVC runtime's command-line parser.
std::string QuoteArg(const std::string& arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
    {
        return arg;
    }
    std::string quoted = "\"";
    for (size_t i = 0; ; ++i)
    {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\')
        {
            ++i;
            ++backslashes;
        }
        if (i == arg.size())
        {
            quoted.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"')
        {
            quoted.append(backslashes * 2 + 1, '\\');
        }
        else
        {
            quoted.append(backslashes, '\\');
        }
        quoted += arg[i];
    }
    return quoted + "\"";
}
}

struct ChildProcess::Impl
{
    HANDLE job{ nullptr };
    HANDLE process{ nullptr };
    HANDLE stdoutRead{ nullptr };
};

ChildProcess::ChildProcess() : m_impl{ new Impl{} } {}
ChildProcess::~ChildProcess() { Kill(); }

bool ChildProcess::Start(const std::vector<std::string>& args, const Options& options, std::string& error)
{
    std::string commandLine;
    for (const auto& a : args)
    {
        commandLine += (commandLine.empty() ? "" : " ") + QuoteArg(a);
    }

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE stdoutWrite = nullptr;
    if (options.captureStdout)
    {
        if (!CreatePipe(&m_impl->stdoutRead, &stdoutWrite, &sa, 0))
        {
            error = "CreatePipe failed (" + std::to_string(GetLastError()) + ")";
            return false;
        }
        SetHandleInformation(m_impl->stdoutRead, HANDLE_FLAG_INHERIT, 0);
    }
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = options.captureStdout ? stdoutWrite : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (options.killWithParent)
    {
        m_impl->job = CreateJobObjectA(nullptr, nullptr);
        if (m_impl->job)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(m_impl->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        }
    }

    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');
    PROCESS_INFORMATION pi{};
    BOOL created = CreateProcessA(nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    DWORD createError = GetLastError();
    if (stdoutWrite)
    {
        CloseHandle(stdoutWrite);
    }
    if (nul != INVALID_HANDLE_VALUE)
    {
        CloseHandle(nul);
    }
    if (!created)
    {
        error = "CreateProcess failed (" + std::to_string(createError) + ")";
        return false;
    }
    // If the job can't be applied (e.g. a restrictive parent job), Kill() still terminates it.
    if (m_impl->job && !AssignProcessToJobObject(m_impl->job, pi.hProcess))
    {
        CloseHandle(m_impl->job);
        m_impl->job = nullptr;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    m_impl->process = pi.hProcess;
    return true;
}

bool ChildProcess::ReadLine(std::string& line, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    line.clear();
    while (m_impl->stdoutRead && std::chrono::steady_clock::now() < deadline)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(m_impl->stdoutRead, nullptr, 0, nullptr, &available, nullptr))
        {
            return false; // the child exited and closed its end
        }
        if (available == 0)
        {
            if (WaitForSingleObject(m_impl->process, 0) == WAIT_OBJECT_0)
            {
                return false;
            }
            Sleep(10);
            continue;
        }
        char c = 0;
        DWORD read = 0;
        if (!ReadFile(m_impl->stdoutRead, &c, 1, &read, nullptr) || read == 0)
        {
            return false;
        }
        if (c == '\n')
        {
            return true;
        }
        if (c != '\r')
        {
            line += c;
        }
    }
    return false;
}

int ChildProcess::Wait(std::chrono::milliseconds timeout)
{
    if (!m_impl->process || WaitForSingleObject(m_impl->process, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0)
    {
        return -1;
    }
    DWORD code = 0;
    GetExitCodeProcess(m_impl->process, &code);
    return static_cast<int>(code);
}

bool ChildProcess::Running()
{
    return m_impl->process && WaitForSingleObject(m_impl->process, 0) == WAIT_TIMEOUT;
}

void ChildProcess::Kill()
{
    if (m_impl->job)
    {
        TerminateJobObject(m_impl->job, 1);
        CloseHandle(m_impl->job);
        m_impl->job = nullptr;
    }
    if (m_impl->process)
    {
        TerminateProcess(m_impl->process, 1);
        WaitForSingleObject(m_impl->process, 5000);
        CloseHandle(m_impl->process);
        m_impl->process = nullptr;
    }
    if (m_impl->stdoutRead)
    {
        CloseHandle(m_impl->stdoutRead);
        m_impl->stdoutRead = nullptr;
    }
}

#else

// ------------------------------------------------------------------------------------------------
// POSIX (Linux, macOS)
// ------------------------------------------------------------------------------------------------

uint32_t ProcessId()
{
    return static_cast<uint32_t>(getpid());
}

std::string GetEnv(const char* name)
{
    const char* value = getenv(name);
    return value ? std::string{ value } : std::string{};
}

void SetEnv(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

std::string ExecutablePath()
{
#if defined(__APPLE__)
    char path[4096] = {};
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
    {
        return {};
    }
    char resolved[4096] = {};
    return realpath(path, resolved) ? std::string{ resolved } : std::string{ path };
#else
    char path[4096] = {};
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    return n > 0 ? std::string(path, static_cast<size_t>(n)) : std::string{};
#endif
}

bool FileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

int StrICmp(const char* a, const char* b)
{
    return strcasecmp(a, b);
}

void ExitImmediately(int code)
{
    fflush(stdout);
    fflush(stderr);
    _exit(code);
}

namespace
{
const char* const* g_crashTestName = nullptr;

void WriteStderr(const char* text)
{
    ssize_t ignored = write(STDERR_FILENO, text, strlen(text));
    (void)ignored;
}

void CrashHandler(int sig)
{
    // Only async-signal-safe calls (backtrace_symbols_fd writes directly to the fd).
    WriteStderr("\n*** libHttpClient.E2E: fatal signal ");
    WriteStderr(sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGABRT ? "SIGABRT" : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : "?");
    WriteStderr(" during test: ");
    WriteStderr((g_crashTestName && *g_crashTestName && **g_crashTestName) ? *g_crashTestName : "<none>");
    WriteStderr("\n");
    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}
}

void InstallCrashHandler(const char* const* currentTest)
{
    g_crashTestName = currentTest;
    // Load libgcc's unwinder now; backtrace() may allocate on first use, which isn't safe in a
    // signal handler.
    void* warmup[1];
    backtrace(warmup, 1);
    for (int sig : { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL })
    {
        signal(sig, CrashHandler);
    }
}

uint16_t UnusedLoopbackPort()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
    {
        return 0;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    uint16_t port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
    {
        socklen_t len = sizeof(addr);
        getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
        port = ntohs(addr.sin_port);
    }
    close(s);
    return port;
}

struct ChildProcess::Impl
{
    pid_t pid{ -1 };
    int stdoutRead{ -1 };
    bool exited{ false };
    int exitCode{ -1 };
    std::string buffered;

    bool Reap(bool block)
    {
        if (pid < 0 || exited)
        {
            return exited;
        }
        int status = 0;
        pid_t r = waitpid(pid, &status, block ? 0 : WNOHANG);
        if (r == pid)
        {
            exited = true;
            exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        }
        return exited;
    }
};

ChildProcess::ChildProcess() : m_impl{ new Impl{} } {}
ChildProcess::~ChildProcess() { Kill(); }

bool ChildProcess::Start(const std::vector<std::string>& args, const Options& options, std::string& error)
{
    // posix_spawn rather than fork/exec: this process is multithreaded (libHttpClient workers).
    int pipeFds[2] = { -1, -1 };
    if (options.captureStdout && pipe(pipeFds) != 0)
    {
        error = std::string{ "pipe failed: " } + strerror(errno);
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (options.captureStdout)
    {
        posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
        posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
    }

    std::vector<char*> argv;
    for (const auto& a : args)
    {
        argv.push_back(const_cast<char*>(a.c_str()));
    }
    argv.push_back(nullptr);

    fflush(stdout);
    fflush(stderr);
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), E2E_ENVIRON);
    posix_spawn_file_actions_destroy(&actions);
    if (options.captureStdout)
    {
        close(pipeFds[1]);
    }
    if (rc != 0)
    {
        if (options.captureStdout)
        {
            close(pipeFds[0]);
        }
        error = std::string{ "posix_spawnp failed: " } + strerror(rc);
        return false;
    }
    m_impl->pid = pid;
    if (options.captureStdout)
    {
        m_impl->stdoutRead = pipeFds[0];
        fcntl(m_impl->stdoutRead, F_SETFD, FD_CLOEXEC);
    }

    // Some libcs report a failed exec as a child that exits 127 instead of an error.
    SleepMs(50);
    if (m_impl->Reap(false) && m_impl->exitCode == 127)
    {
        error = "could not execute " + args[0];
        return false;
    }
    return true;
}

bool ChildProcess::ReadLine(std::string& line, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    line.clear();
    while (m_impl->stdoutRead >= 0)
    {
        size_t newline = m_impl->buffered.find('\n');
        if (newline != std::string::npos)
        {
            line = m_impl->buffered.substr(0, newline);
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            m_impl->buffered.erase(0, newline + 1);
            return true;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0)
        {
            return false;
        }
        pollfd pfd{ m_impl->stdoutRead, POLLIN, 0 };
        int r = poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (r < 0 && errno == EINTR)
        {
            continue;
        }
        if (r <= 0)
        {
            return false;
        }
        char chunk[512];
        ssize_t n = read(m_impl->stdoutRead, chunk, sizeof(chunk));
        if (n <= 0)
        {
            return false; // EOF: the child exited or closed stdout
        }
        m_impl->buffered.append(chunk, static_cast<size_t>(n));
    }
    return false;
}

int ChildProcess::Wait(std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!m_impl->Reap(false))
    {
        if (m_impl->pid < 0 || std::chrono::steady_clock::now() >= deadline)
        {
            return -1;
        }
        SleepMs(10);
    }
    return m_impl->exitCode;
}

bool ChildProcess::Running()
{
    return m_impl->pid > 0 && !m_impl->Reap(false);
}

void ChildProcess::Kill()
{
    if (m_impl->pid > 0 && !m_impl->Reap(false))
    {
        kill(m_impl->pid, SIGKILL);
        m_impl->Reap(true);
    }
    m_impl->pid = -1;
    if (m_impl->stdoutRead >= 0)
    {
        close(m_impl->stdoutRead);
        m_impl->stdoutRead = -1;
    }
}

#endif

} // namespace platform
} // namespace e2e
