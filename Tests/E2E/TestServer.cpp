// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "TestServer.h"

#include <cstdio>

namespace e2e
{

namespace
{

std::string FindServerScript()
{
    const std::string relative = std::string{ "Tests" } + platform::PathSeparator + "E2E" + platform::PathSeparator + "server" + platform::PathSeparator + "lhc_test_server.py";
    std::string dir = platform::ExecutablePath();
    // Out/<platform>/<config>/<project>/<exe> is 4 levels below the repo root; search a little
    // further in case the exe was copied elsewhere in the tree.
    for (int level = 0; level < 8; ++level)
    {
        size_t slash = dir.find_last_of("\\/");
        if (slash == std::string::npos || slash == 0)
        {
            break;
        }
        dir.resize(slash);
        std::string candidate = dir + platform::PathSeparator + relative;
        if (platform::FileExists(candidate))
        {
            return candidate;
        }
    }
    return {};
}

} // namespace

TestServer::~TestServer()
{
    Stop();
}

bool TestServer::Launch(const LaunchOptions& options, ServerInfo& info, std::string& error)
{
    m_script = options.script.empty() ? FindServerScript() : options.script;
    if (m_script.empty() || !platform::FileExists(m_script))
    {
        error = "could not find Tests/E2E/server/lhc_test_server.py (set LHC_E2E_SERVER_SCRIPT or pass --server-script)";
        return false;
    }

    // --timeout is a backstop in case this process is killed in a way that skips the cleanup.
    std::vector<std::string> serverArgs{ "-u", m_script, "--timeout", "3600" };
#if !defined(_WIN32)
    serverArgs.push_back("--parent-pid");
    serverArgs.push_back(std::to_string(platform::ProcessId()));
#endif
    if (options.verbose)
    {
        serverArgs.push_back("--verbose");
    }

    std::vector<std::vector<std::string>> interpreters;
    if (!options.python.empty())
    {
        interpreters.push_back({ options.python });
    }
    else
    {
#if defined(_WIN32)
        interpreters.push_back({ "python" });
        interpreters.push_back({ "py", "-3" });
#else
        interpreters.push_back({ "python3" });
        interpreters.push_back({ "python" });
#endif
    }

    std::string errors;
    for (auto args : interpreters)
    {
        args.insert(args.end(), serverArgs.begin(), serverArgs.end());
        std::string attemptError;
        if (TryStart(args, info, attemptError))
        {
            return true;
        }
        errors += "\n  " + args[0] + ": " + attemptError;
        Stop();
    }
    error = "no usable Python 3 found:" + errors;
    return false;
}

bool TestServer::TryStart(const std::vector<std::string>& args, ServerInfo& info, std::string& error)
{
    m_process.reset(new platform::ChildProcess{});
    platform::ChildProcess::Options options;
    options.captureStdout = true;
    options.killWithParent = true;
    if (!m_process->Start(args, options, error))
    {
        return false;
    }

    std::string line;
    if (!m_process->ReadLine(line, std::chrono::seconds{ 30 }))
    {
        int code = m_process->Wait(std::chrono::milliseconds{ 0 });
        error = Format("server did not report its ports (exit code %d, output '%s')", code, line.c_str());
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
    if (m_process)
    {
        m_process->Kill();
        m_process.reset();
    }
}

} // namespace e2e
