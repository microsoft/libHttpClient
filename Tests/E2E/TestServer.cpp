// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "TestServer.h"

#include <cstdio>

namespace e2e
{

namespace
{

bool FileExists(const std::string& path)
{
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string FindServerScript()
{
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir = exePath;
    // Out\<platform>\<config>\<project>\<exe> is 4 levels below the repo root; search a little
    // further in case the exe was copied elsewhere in the tree.
    for (int level = 0; level < 8; ++level)
    {
        size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos)
        {
            break;
        }
        dir.resize(slash);
        std::string candidate = dir + "\\Tests\\E2E\\server\\lhc_test_server.py";
        if (FileExists(candidate))
        {
            return candidate;
        }
    }
    return {};
}

// Reads one '\n'-terminated line from the pipe, giving up after `timeout`.
bool ReadLine(HANDLE pipe, HANDLE process, std::string& line, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    line.clear();
    while (std::chrono::steady_clock::now() < deadline)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
        {
            return false; // the child exited and closed its end
        }
        if (available == 0)
        {
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
            {
                return false;
            }
            Sleep(10);
            continue;
        }
        char c = 0;
        DWORD read = 0;
        if (!ReadFile(pipe, &c, 1, &read, nullptr) || read == 0)
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

} // namespace

TestServer::~TestServer()
{
    Stop();
}

bool TestServer::Launch(const LaunchOptions& options, ServerInfo& info, std::string& error)
{
    m_script = options.script.empty() ? FindServerScript() : options.script;
    if (m_script.empty() || !FileExists(m_script))
    {
        error = "could not find Tests\\E2E\\server\\lhc_test_server.py (set LHC_E2E_SERVER_SCRIPT or pass --server-script)";
        return false;
    }

    // --timeout is a backstop in case this process is killed in a way that skips the job object.
    std::string args = " -u \"" + m_script + "\" --timeout 3600" + (options.verbose ? " --verbose" : "");
    std::vector<std::string> candidates;
    if (!options.python.empty())
    {
        candidates.push_back("\"" + options.python + "\"" + args);
    }
    else
    {
        candidates.push_back("python" + args);
        candidates.push_back("py -3" + args);
    }

    std::string errors;
    for (const auto& commandLine : candidates)
    {
        std::string attemptError;
        if (TryStart(commandLine, info, attemptError))
        {
            return true;
        }
        errors += "\n  " + commandLine + ": " + attemptError;
        Stop();
    }
    error = "no usable Python 3 found:" + errors;
    return false;
}

bool TestServer::TryStart(const std::string& commandLine, ServerInfo& info, std::string& error)
{
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE stdoutWrite = nullptr;
    if (!CreatePipe(&m_stdoutRead, &stdoutWrite, &sa, 0))
    {
        error = Format("CreatePipe failed (%lu)", GetLastError());
        return false;
    }
    SetHandleInformation(m_stdoutRead, HANDLE_FLAG_INHERIT, 0);

    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = stdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE); // server diagnostics go to our stderr

    m_job = CreateJobObjectA(nullptr, nullptr);
    if (m_job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }

    std::vector<char> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back('\0');
    PROCESS_INFORMATION pi{};
    BOOL created = CreateProcessA(nullptr, mutableCommandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    DWORD createError = GetLastError();
    CloseHandle(stdoutWrite);
    if (nul != INVALID_HANDLE_VALUE)
    {
        CloseHandle(nul);
    }
    if (!created)
    {
        error = Format("CreateProcess failed (%lu)", createError);
        return false;
    }

    // If the job can't be applied (e.g. a restrictive parent job), Stop() still terminates the
    // process directly.
    if (m_job && !AssignProcessToJobObject(m_job, pi.hProcess))
    {
        CloseHandle(m_job);
        m_job = nullptr;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    m_process = pi.hProcess;

    std::string line;
    if (!ReadLine(m_stdoutRead, m_process, line, std::chrono::seconds{ 30 }))
    {
        DWORD code = 0;
        GetExitCodeProcess(m_process, &code);
        error = Format("server did not report its ports (exit code %lu, output '%s')", code, line.c_str());
        return false;
    }

    std::string http, http2, https;
    if (!JsonField(line, "http", http) || !JsonField(line, "http2", http2) || !JsonField(line, "https", https))
    {
        error = "unexpected server ready line: " + line;
        return false;
    }
    info.http = static_cast<uint16_t>(atoi(http.c_str()));
    info.http2 = static_cast<uint16_t>(atoi(http2.c_str()));
    info.https = static_cast<uint16_t>(atoi(https.c_str())); // "null" -> 0: HTTPS unavailable
    std::string httpsError;
    if (JsonField(line, "https_error", httpsError) && httpsError != "null")
    {
        info.httpsError = httpsError;
    }
    return info.http != 0 && info.http2 != 0;
}

void TestServer::Stop()
{
    if (m_job)
    {
        TerminateJobObject(m_job, 0);
        CloseHandle(m_job);
        m_job = nullptr;
    }
    if (m_process)
    {
        TerminateProcess(m_process, 0);
        WaitForSingleObject(m_process, 5000);
        CloseHandle(m_process);
        m_process = nullptr;
    }
    if (m_stdoutRead)
    {
        CloseHandle(m_stdoutRead);
        m_stdoutRead = nullptr;
    }
}

} // namespace e2e
