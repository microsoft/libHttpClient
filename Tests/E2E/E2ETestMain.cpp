// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// Test runner for the libHttpClient end-to-end tests. See Tests/E2E/README.md.
//
//   libHttpClient.E2E.Win32.exe / libHttpClient.E2E [--filter <substring>] [--list] [--ports <http>,<http2>,<https>]
//                               [--server-script <path>] [--python <exe>] [--test-timeout <seconds>]
//                               [--trace] [--server-verbose]
//
// Exit code = number of failed tests (0 = all passed or skipped).

#include "E2ETestFramework.h"
#include "E2EPlatform.h"
#include "TestServer.h"

#include <httpClient/trace.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <thread>

namespace e2e
{

namespace
{
ServerInfo g_server;
std::atomic<uint64_t> g_uniqueCounter{ 0 };
thread_local const char* t_currentTest = "";
// The test the runner is currently executing (read by the crash handler from any thread).
const char* volatile g_runningTest = "";
std::mutex g_poisonMutex;
std::string g_poisonReason;
std::atomic<bool> g_poisoned{ false };
}

std::vector<TestInfo>& Registry()
{
    static std::vector<TestInfo> tests;
    return tests;
}

const ServerInfo& Server()
{
    return g_server;
}

void RequireHttps()
{
    if (g_server.https == 0)
    {
        Skip("the test server has no HTTPS endpoint (it generates its certificate with the openssl CLI, "
            "looked up on PATH and then in Git for Windows): " + (g_server.httpsError.empty() ? std::string{ "port 0 was passed with --ports" } : g_server.httpsError));
    }
}

std::string HttpUrl(const std::string& path) { return Format("http://127.0.0.1:%u", g_server.http) + path; }
std::string Http2Url(const std::string& path) { return Format("http://127.0.0.1:%u", g_server.http2) + path; }
std::string HttpsUrl(const std::string& path) { return Format("https://127.0.0.1:%u", g_server.https) + path; }
std::string WsUrl(const std::string& path) { return Format("ws://127.0.0.1:%u", g_server.http) + path; }

std::string UniqueId(const char* prefix)
{
    return Format("%s-%lu-%llu-%llu", prefix, platform::ProcessId(), static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()), static_cast<unsigned long long>(++g_uniqueCounter));
}

std::string Format(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

std::string HrToString(HRESULT hr)
{
    return Format("0x%08X", static_cast<unsigned>(hr));
}

void Info(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    printf("    [%s] %s\n", t_currentTest, buffer);
    fflush(stdout);
}

void Fail(const char* file, int line, const std::string& message)
{
    const char* base = strrchr(file, platform::PathSeparator);
    throw TestFailure{ Format("%s(%d): ", base ? base + 1 : file, line) + message };
}

void Skip(const std::string& reason)
{
    throw TestSkipped{ reason };
}

bool JsonField(const std::string& json, const std::string& key, std::string& value)
{
    std::string needle = "\"" + key + "\":";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
        return false;
    }
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    {
        ++pos;
    }
    if (pos >= json.size())
    {
        return false;
    }
    value.clear();
    if (json[pos] == '"')
    {
        for (size_t i = pos + 1; i < json.size(); ++i)
        {
            char c = json[i];
            if (c == '"')
            {
                return true;
            }
            if (c == '\\' && i + 1 < json.size())
            {
                char e = json[++i];
                switch (e)
                {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case 'u':
                    // The server emits ensure_ascii=False, so \u only appears for control characters.
                    if (i + 4 < json.size())
                    {
                        value += static_cast<char>(strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16));
                        i += 4;
                    }
                    break;
                default: value += e; break;
                }
                continue;
            }
            value += c;
        }
        return false;
    }
    size_t end = json.find_first_of(",}]", pos);
    value = json.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    return true;
}

std::string JsonFieldOr(const std::string& json, const std::string& key, const std::string& fallback)
{
    std::string value;
    return JsonField(json, key, value) ? value : fallback;
}

uint64_t JsonUInt(const std::string& json, const std::string& key)
{
    std::string value;
    if (!JsonField(json, key, value))
    {
        Fail(__FILE__, __LINE__, "server JSON has no field \"" + key + "\": " + json.substr(0, 400));
    }
    return strtoull(value.c_str(), nullptr, 10);
}

std::vector<uint8_t> PatternBytes(size_t offset, size_t length)
{
    std::vector<uint8_t> data(length);
    for (size_t i = 0; i < length; ++i)
    {
        data[i] = static_cast<uint8_t>((offset + i) % 251);
    }
    return data;
}

bool IsPattern(const uint8_t* data, size_t length, size_t offset)
{
    for (size_t i = 0; i < length; ++i)
    {
        if (data[i] != static_cast<uint8_t>((offset + i) % 251))
        {
            return false;
        }
    }
    return true;
}

uint32_t Crc32(const uint8_t* data, size_t length)
{
    static uint32_t table[256] = {};
    static std::once_flag once;
    std::call_once(once, []
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
    });
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
    {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

HRESULT WaitAsync(XAsyncBlock* async, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true)
    {
        HRESULT hr = XAsyncGetStatus(async, false);
        if (hr != E_PENDING)
        {
            return hr;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return E_PENDING;
        }
        platform::SleepMs(2);
    }
}

HRESULT CleanupWithTimeout(std::chrono::milliseconds timeout)
{
    // Heap-allocated and leaked if cleanup hangs, so the still-running operation never touches
    // freed memory.
    auto* async = new XAsyncBlock{};
    HRESULT hr = HCCleanupAsync(async);
    if (FAILED(hr))
    {
        delete async;
        return hr;
    }
    hr = WaitAsync(async, timeout);
    if (hr != E_PENDING)
    {
        delete async;
    }
    return hr;
}

void PoisonProcess(const std::string& reason)
{
    std::lock_guard<std::mutex> lock{ g_poisonMutex };
    if (!g_poisoned)
    {
        g_poisonReason = reason;
        g_poisoned = true;
    }
}

} // namespace e2e

using namespace e2e;

namespace
{

enum class Outcome { Pass, Fail, Skip, NotRun };

struct Result
{
    std::string name;
    Outcome outcome{ Outcome::NotRun };
    std::string detail;
    double seconds{ 0 };
};

void CALLBACK TraceToStderr(const char* area, HCTraceLevel level, uint64_t threadId, uint64_t, const char* message)
{
    fprintf(stderr, "[hc %s %d tid=%llu] %s\n", area, static_cast<int>(level), static_cast<unsigned long long>(threadId), message);
}

void RunTestBody(const TestInfo& test, Result& result)
{
    t_currentTest = test.name;
    try
    {
        if (HCIsInitialized())
        {
            throw TestFailure{ "libHttpClient is still initialized from a previous test" };
        }
        if (!test.selfInit)
        {
            HRESULT hr = HCInitialize(nullptr);
            if (FAILED(hr))
            {
                throw TestFailure{ "HCInitialize failed: " + HrToString(hr) };
            }
        }

        try
        {
            test.func();
            result.outcome = Outcome::Pass;
        }
        catch (const TestFailure& f)
        {
            result.outcome = Outcome::Fail;
            result.detail = f.message;
        }
        catch (const TestSkipped& s)
        {
            result.outcome = Outcome::Skip;
            result.detail = s.reason;
        }
        catch (const std::exception& e)
        {
            result.outcome = Outcome::Fail;
            result.detail = std::string{ "unexpected exception: " } + e.what();
        }

        if (!g_poisoned && HCIsInitialized())
        {
            // Every test must leave libHttpClient able to clean up promptly.
            HRESULT hr = CleanupWithTimeout(std::chrono::seconds{ 20 });
            if (hr == E_PENDING)
            {
                result.outcome = Outcome::Fail;
                result.detail += (result.detail.empty() ? "" : "; ") + std::string{ "HCCleanupAsync did not complete within 20s after the test" };
                PoisonProcess("HCCleanupAsync hung after " + std::string{ test.name });
            }
            else if (FAILED(hr) && result.outcome == Outcome::Pass)
            {
                result.outcome = Outcome::Fail;
                result.detail = "HCCleanupAsync failed: " + HrToString(hr);
            }
        }
    }
    catch (const TestFailure& f)
    {
        result.outcome = Outcome::Fail;
        result.detail = f.message;
    }
}

struct Options
{
    std::vector<std::string> filters;
    std::string exact;
    bool child{ false };
    bool list{ false };
    std::string ports;
    std::string serverScript;
    std::string python;
    int testTimeoutSeconds{ 120 };
    bool trace{ false };
    bool serverVerbose{ false };
};

// Exit codes a --child run uses to report its single test's outcome.
constexpr int ChildExitPass = 0;
constexpr int ChildExitFail = 1;
constexpr int ChildExitSkip = 77;

// Runs one isolated test as "<this exe> --child --exact <name> --ports ..." sharing our console.
Result RunIsolated(const TestInfo& test, const Options& options)
{
    Result result{ test.name };
    std::vector<std::string> args{
        platform::ExecutablePath(), "--child", "--exact", test.name,
        "--ports", Format("%u,%u,%u", g_server.http, g_server.http2, g_server.https),
        "--test-timeout", std::to_string(options.testTimeoutSeconds) };
    if (options.trace)
    {
        args.push_back("--trace");
    }

    platform::ChildProcess child;
    std::string error;
    if (!child.Start(args, platform::ChildProcess::Options{}, error))
    {
        result.outcome = Outcome::Fail;
        result.detail = "could not start the child process: " + error;
        return result;
    }
    int code = child.Wait(std::chrono::seconds{ options.testTimeoutSeconds + 30 });
    switch (code)
    {
    case -1:
        child.Kill();
        result.outcome = Outcome::Fail;
        result.detail = "isolated child process did not exit";
        break;
    case ChildExitPass: result.outcome = Outcome::Pass; break;
    case ChildExitSkip: result.outcome = Outcome::Skip; result.detail = "skipped in the isolated child (see its output above)"; break;
    default: result.outcome = Outcome::Fail; result.detail = Format("isolated child exited with %d (see its output above)", code); break;
    }
    return result;
}

bool ParseArgs(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto next = [&](std::string& out) -> bool
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "missing value for %s\n", arg.c_str());
                return false;
            }
            out = argv[++i];
            return true;
        };
        std::string value;
        if (arg == "--filter") { if (!next(value)) return false; options.filters.push_back(value); }
        else if (arg == "--exact") { if (!next(options.exact)) return false; }
        else if (arg == "--child") { options.child = true; }
        else if (arg == "--list") { options.list = true; }
        else if (arg == "--ports") { if (!next(options.ports)) return false; }
        else if (arg == "--server-script") { if (!next(options.serverScript)) return false; }
        else if (arg == "--python") { if (!next(options.python)) return false; }
        else if (arg == "--test-timeout") { if (!next(value)) return false; options.testTimeoutSeconds = atoi(value.c_str()); }
        else if (arg == "--trace") { options.trace = true; }
        else if (arg == "--server-verbose") { options.serverVerbose = true; }
        else
        {
            fprintf(stderr,
                "usage: %s [--filter <substring>]... [--list] [--ports <http>,<http2>,<https|0>]\n"
                "          [--server-script <path>] [--python <exe>] [--test-timeout <seconds>] [--trace] [--server-verbose]\n"
                "Environment: LHC_E2E_PORTS, LHC_E2E_PYTHON, LHC_E2E_SERVER_SCRIPT\n", argv[0]);
            return false;
        }
    }
    return true;
}

bool Matches(const Options& options, const char* name)
{
    if (!options.exact.empty())
    {
        return options.exact == name;
    }
    if (options.filters.empty())
    {
        return true;
    }
    for (const auto& f : options.filters)
    {
        if (strstr(name, f.c_str()) != nullptr)
        {
            return true;
        }
    }
    return false;
}

std::string EnvOr(const char* name, const std::string& fallback)
{
    std::string value = platform::GetEnv(name);
    return value.empty() ? fallback : value;
}

} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    platform::InstallCrashHandler(const_cast<const char* const*>(&g_runningTest));

    Options options;
    if (!ParseArgs(argc, argv, options))
    {
        return 1000;
    }

    std::vector<size_t> order;
    for (size_t i = 0; i < Registry().size(); ++i)
    {
        if (Matches(options, Registry()[i].name))
        {
            order.push_back(i);
        }
    }
    std::stable_sort(order.begin(), order.end(), [](size_t a, size_t b) { return Registry()[a].order < Registry()[b].order; });

    if (options.list)
    {
        for (size_t i : order)
        {
            printf("%s\n", Registry()[i].name);
        }
        return 0;
    }
    if (order.empty())
    {
        fprintf(stderr, "No tests match the filter.\n");
        return 1000;
    }

    if (options.trace)
    {
        HCSettingsSetTraceLevel(HCTraceLevel::Verbose);
        HCTraceSetClientCallback(TraceToStderr);
    }

    TestServer server;
    std::string ports = options.ports.empty() ? EnvOr("LHC_E2E_PORTS", "") : options.ports;
    if (!ports.empty())
    {
        unsigned a = 0, b = 0, c = 0;
        if (sscanf(ports.c_str(), "%u,%u,%u", &a, &b, &c) != 3)
        {
            fprintf(stderr, "Invalid --ports / LHC_E2E_PORTS value '%s' (expected <http>,<http2>,<https>)\n", ports.c_str());
            return 1000;
        }
        g_server.http = static_cast<uint16_t>(a);
        g_server.http2 = static_cast<uint16_t>(b);
        g_server.https = static_cast<uint16_t>(c);
        // Set by the parent for isolated child runs, so HTTPS skips explain themselves.
        g_server.httpsError = EnvOr("LHC_E2E_HTTPS_ERROR", "");
        if (!options.child)
        {
            printf("Using externally started test server: http=%u http2=%u https=%u%s\n", a, b, c, c == 0 ? " (HTTPS tests will be skipped)" : "");
        }
    }
    else
    {
        TestServer::LaunchOptions launch;
        launch.script = options.serverScript.empty() ? EnvOr("LHC_E2E_SERVER_SCRIPT", "") : options.serverScript;
        launch.python = options.python.empty() ? EnvOr("LHC_E2E_PYTHON", "") : options.python;
        launch.verbose = options.serverVerbose;
        std::string error;
        if (!server.Launch(launch, g_server, error))
        {
            fprintf(stderr, "ERROR: could not start the loopback test server: %s\n", error.c_str());
            return 1000;
        }
        printf("Started loopback test server (%s): http=%u http2=%u https=%u\n", server.ScriptPath().c_str(), g_server.http, g_server.http2, g_server.https);
        if (g_server.https == 0)
        {
            printf("WARNING: HTTPS is unavailable, so the HTTPS tests will be skipped: %s\n", g_server.httpsError.c_str());
            platform::SetEnv("LHC_E2E_HTTPS_ERROR", g_server.httpsError);
        }
    }

    auto suiteStart = std::chrono::steady_clock::now();
    std::vector<Result> results;
    results.reserve(order.size());

    for (size_t index : order)
    {
        const TestInfo& test = Registry()[index];
        results.push_back(Result{ test.name });
        Result& result = results.back();
        if (g_poisoned)
        {
            result.detail = "not run: " + g_poisonReason;
            printf("[ NOTRUN ] %s (%s)\n", test.name, result.detail.c_str());
            continue;
        }

        if (!options.child)
        {
            printf("[ RUN    ] %s%s\n", test.name, test.isolated ? " (isolated child process)" : "");
        }
        g_runningTest = test.name;
        auto start = std::chrono::steady_clock::now();

        if (test.isolated && !options.child)
        {
            result = RunIsolated(test, options);
        }
        else
        {
            // The body runs on its own thread so a hung test turns into a reported failure.
            struct Shared
            {
                Event done;
                Result result;
            };
            auto shared = std::make_shared<Shared>();
            shared->result.name = test.name;
            std::thread worker([shared, &test]
            {
                RunTestBody(test, shared->result);
                shared->done.Set();
            });

            if (shared->done.Wait(std::chrono::seconds{ options.testTimeoutSeconds }))
            {
                worker.join();
                result = shared->result;
            }
            else
            {
                worker.detach();
                result.outcome = Outcome::Fail;
                result.detail = Format("test did not finish within %d s", options.testTimeoutSeconds);
                PoisonProcess(std::string{ "test " } + test.name + " hung");
            }
        }

        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (options.child)
        {
            // The parent prints the result line; just pass on why it didn't pass.
            if (result.outcome != Outcome::Pass)
            {
                printf("    [%s] %s: %s\n", test.name, result.outcome == Outcome::Skip ? "skipped" : "failed", result.detail.c_str());
            }
            continue;
        }
        switch (result.outcome)
        {
        case Outcome::Pass: printf("[   PASS ] %s (%.2f s)\n", test.name, result.seconds); break;
        case Outcome::Skip: printf("[   SKIP ] %s (%.2f s): %s\n", test.name, result.seconds, result.detail.c_str()); break;
        default: printf("[   FAIL ] %s (%.2f s): %s\n", test.name, result.seconds, result.detail.c_str()); break;
        }
    }

    g_runningTest = "<none: after the last test, during summary / process exit>";
    double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - suiteStart).count();
    int passed = 0, failed = 0, skipped = 0, notRun = 0;
    for (const auto& r : results)
    {
        switch (r.outcome)
        {
        case Outcome::Pass: ++passed; break;
        case Outcome::Fail: ++failed; break;
        case Outcome::Skip: ++skipped; break;
        case Outcome::NotRun: ++notRun; break;
        }
    }

    if (options.child)
    {
        int childCode = (failed + notRun) > 0 ? ChildExitFail : (skipped > 0 ? ChildExitSkip : ChildExitPass);
        fflush(stdout);
        fflush(stderr);
        // Isolated tests may leave libHttpClient work in flight on purpose; don't run destructors.
        platform::ExitImmediately(childCode);
    }

    printf("\n==== libHttpClient E2E summary: %d passed, %d failed, %d skipped, %d not run (%zu total) in %.1f s ====\n",
        passed, failed, skipped, notRun, results.size(), total);
    for (const auto& r : results)
    {
        if (r.outcome == Outcome::Fail || r.outcome == Outcome::NotRun)
        {
            printf("  %s %s: %s\n", r.outcome == Outcome::Fail ? "FAILED " : "NOT RUN", r.name.c_str(), r.detail.c_str());
        }
    }
    for (const auto& r : results)
    {
        if (r.outcome == Outcome::Skip)
        {
            printf("  SKIPPED %s: %s\n", r.name.c_str(), r.detail.c_str());
        }
    }

    // Tests that didn't run because the process was poisoned count as failures.
    int exitCode = failed + notRun;
    server.Stop();
    if (options.trace)
    {
        HCTraceRemoveClientCallback(TraceToStderr);
    }

    if (g_poisoned)
    {
        // libHttpClient may still have work in flight (that's why we're poisoned); don't let static
        // destructors race it.
        fflush(stdout);
        fflush(stderr);
        platform::ExitImmediately(exitCode);
    }
    return exitCode;
}
