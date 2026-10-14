// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// Minimal test framework for the libHttpClient end-to-end tests. The tests run against the real
// platform providers (WinHTTP on Win32) and the loopback server in Tests/E2E/server.
//
// Register a test with E2E_TEST(name) { ... } in any .cpp in this project. By default the runner
// calls HCInitialize() before the test and HCCleanupAsync() after it (and fails the test if
// cleanup doesn't complete). Use E2E_TEST_SELF_INIT(name) for tests that manage HCInitialize /
// HCCleanup themselves.

#pragma once

#include <windows.h>
#include <httpClient/httpClient.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace e2e
{

struct TestFailure
{
    std::string message;
};

struct TestSkipped
{
    std::string reason;
};

struct TestInfo
{
    const char* name;
    void (*func)();
    bool selfInit;
    int order; // tests run in ascending order, then registration order
    bool isolated; // run in a child process (for tests that can leave libHttpClient unusable)
};

std::vector<TestInfo>& Registry();

struct TestRegistrar
{
    TestRegistrar(const char* name, void (*func)(), bool selfInit, int order, bool isolated = false)
    {
        Registry().push_back(TestInfo{ name, func, selfInit, order, isolated });
    }
};

// Loopback server endpoints (filled in by the runner before any test runs).
struct ServerInfo
{
    uint16_t http{ 0 };
    uint16_t http2{ 0 };
    uint16_t https{ 0 };         // 0 = HTTPS unavailable (the server couldn't make a certificate)
    std::string httpsError;      // why HTTPS is unavailable, if it is
};

const ServerInfo& Server();

// Skips the current test if the server has no HTTPS endpoint.
void RequireHttps();

// "http://127.0.0.1:<http port><path>"
std::string HttpUrl(const std::string& path);
// "http://127.0.0.1:<http2 port><path>"
std::string Http2Url(const std::string& path);
// "https://127.0.0.1:<https port><path>"
std::string HttpsUrl(const std::string& path);
// "ws://127.0.0.1:<http port><path>"
std::string WsUrl(const std::string& path);

// A unique token for keying per-test server state (retry counters, WebSocket logs, ...).
std::string UniqueId(const char* prefix);

// Informational output, prefixed with the current test name.
void Info(const char* format, ...);

[[noreturn]] void Fail(const char* file, int line, const std::string& message);
[[noreturn]] void Skip(const std::string& reason);

std::string Format(const char* format, ...);
std::string HrToString(HRESULT hr);

// Value of a top-level-or-nested "key":<value> pair in the server's canonical JSON
// (json.dumps(sort_keys=True, separators=(',', ':'))). Returns the raw token for numbers / bools
// and the unescaped text for strings. Returns false if the key isn't present.
bool JsonField(const std::string& json, const std::string& key, std::string& value);
std::string JsonFieldOr(const std::string& json, const std::string& key, const std::string& fallback = "<missing>");
uint64_t JsonUInt(const std::string& json, const std::string& key);

// The server's deterministic body content: byte i is (i % 251).
std::vector<uint8_t> PatternBytes(size_t offset, size_t length);
bool IsPattern(const uint8_t* data, size_t length, size_t offset = 0);
uint32_t Crc32(const uint8_t* data, size_t length);

// Simple event: Set() once, Wait() with timeout.
class Event
{
public:
    void Set()
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_set = true;
        m_cv.notify_all();
    }
    bool Wait(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock{ m_mutex };
        return m_cv.wait_for(lock, timeout, [this] { return m_set; });
    }
    bool IsSet()
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        return m_set;
    }
private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_set{ false };
};

// Waits for an XAsyncBlock to complete, polling so a hang turns into a failure instead of a stuck
// process. Returns E_PENDING if it didn't complete in time.
HRESULT WaitAsync(XAsyncBlock* async, std::chrono::milliseconds timeout);

// Runs HCCleanupAsync and waits up to `timeout`. Returns E_PENDING if cleanup is still running.
HRESULT CleanupWithTimeout(std::chrono::milliseconds timeout);

// Marks the process as unable to continue (e.g. HCCleanupAsync hung). The runner reports the
// remaining tests as not run and exits without running static destructors.
void PoisonProcess(const std::string& reason);

} // namespace e2e

#define E2E_CONCAT_INNER(a, b) a##b
#define E2E_CONCAT(a, b) E2E_CONCAT_INNER(a, b)

#define E2E_TEST_IMPL(name, selfInit, order, isolated) \
    static void E2E_CONCAT(e2e_test_, name)(); \
    static e2e::TestRegistrar E2E_CONCAT(e2e_registrar_, name){ #name, &E2E_CONCAT(e2e_test_, name), selfInit, order, isolated }; \
    static void E2E_CONCAT(e2e_test_, name)()

#define E2E_TEST(name) E2E_TEST_IMPL(name, false, 0, false)
#define E2E_TEST_SELF_INIT(name) E2E_TEST_IMPL(name, true, 0, false)
// Runs in a child process with its own HCInitialize / HCCleanup, so a known bug that leaves
// libHttpClient unusable (e.g. HCCleanupAsync never completing) can't take the rest of the suite
// down with it.
#define E2E_TEST_ISOLATED(name) E2E_TEST_IMPL(name, true, 0, true)

#define E2E_CHECK(cond) \
    do { if (!(cond)) { e2e::Fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); } } while (0)

#define E2E_CHECK_MSG(cond, ...) \
    do { if (!(cond)) { e2e::Fail(__FILE__, __LINE__, std::string("CHECK(" #cond ") failed: ") + e2e::Format(__VA_ARGS__)); } } while (0)

#define E2E_CHECK_HR(expr) \
    do { HRESULT e2e_hr_ = (expr); if (FAILED(e2e_hr_)) { e2e::Fail(__FILE__, __LINE__, std::string(#expr " failed: ") + e2e::HrToString(e2e_hr_)); } } while (0)

#define E2E_CHECK_EQ(expected, actual) \
    do { \
        auto e2e_e_ = (expected); auto e2e_a_ = (actual); \
        if (!(e2e_e_ == e2e_a_)) { \
            e2e::Fail(__FILE__, __LINE__, std::string("CHECK_EQ(" #expected ", " #actual ") failed: expected ") + e2e::ToDisplay(e2e_e_) + ", got " + e2e::ToDisplay(e2e_a_)); \
        } \
    } while (0)

namespace e2e
{
inline std::string ToDisplay(const std::string& s) { return "\"" + (s.size() > 300 ? s.substr(0, 300) + "..." : s) + "\""; }
inline std::string ToDisplay(const char* s) { return s ? ToDisplay(std::string{ s }) : std::string{ "<null>" }; }
inline std::string ToDisplay(bool b) { return b ? "true" : "false"; }
template<typename T>
std::string ToDisplay(T v) { return std::to_string(v); }
}
