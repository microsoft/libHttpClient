// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "E2ETestFramework.h"

namespace e2e
{

// Starts Tests/E2E/server/lhc_test_server.py in a child process, reads the JSON port line it
// prints, and kills it (via a kill-on-close job object) when stopped or when this process exits.
class TestServer
{
public:
    struct LaunchOptions
    {
        std::string script; // empty = search upwards from the exe for Tests\E2E\server\lhc_test_server.py
        std::string python; // empty = try "python", then "py -3"
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
    bool TryStart(const std::string& commandLine, ServerInfo& info, std::string& error);

    HANDLE m_job{ nullptr };
    HANDLE m_process{ nullptr };
    HANDLE m_stdoutRead{ nullptr };
    std::string m_script;
};

} // namespace e2e
