// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "E2ETestFramework.h"
#include "E2EPlatform.h"

namespace e2e
{

// Starts Tests/E2E/server/lhc_test_server.py in a child process, reads the JSON port line it
// prints, and kills it when stopped. If this process dies without stopping it, the server still
// exits: on Windows via a kill-on-close job object, elsewhere via the server's --parent-pid
// watchdog (and its --timeout backstop).
class TestServer
{
public:
    struct LaunchOptions
    {
        std::string script; // empty = search upwards from the exe for Tests/E2E/server/lhc_test_server.py
        std::string python; // empty = try "python" then "py -3" (Windows) or "python3" then "python"
        bool verbose{ false };
    };

    TestServer() = default;
    ~TestServer();
    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    bool Launch(const LaunchOptions& options, ServerInfo& info, std::string& error);
    void Stop();
    const std::string& ScriptPath() const { return m_script; }

private:
    bool TryStart(const std::vector<std::string>& args, ServerInfo& info, std::string& error);

    std::unique_ptr<platform::ChildProcess> m_process;
    std::string m_script;
};

} // namespace e2e
