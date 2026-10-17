// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

// Per-test isolation for the unit tests.
//
// Every test class gets a per-test-method setup and cleanup through DEFINE_TEST_CLASS_PROPS (see
// DefineTestMacros.h) that calls SetUpTest() before and TearDownTest() after each test method. Both
// run ResetLibraryState(), so a test that fails part way through (a failed VERIFY throws out of the
// test before its own HCCleanup) cannot leave libHttpClient initialized or leave callbacks, mocks,
// memory hooks or settings behind for the tests that follow.
//
// Tests can also call ResetLibraryState() directly when they need a clean slate in the middle of a
// test.
class TestEnvironment
{
public:
    struct ResetResult
    {
        // State that a test is expected to clean up itself. Reported as a test failure by the
        // per-test cleanup (a test that already failed just gets an extra message).
        std::vector<std::string> leaks;

        // Process-wide state that was not at its default and was silently restored. Logged only:
        // there is no public API to undo some of these (for example the perform/WebSocket callbacks),
        // so tests cannot always restore them.
        std::vector<std::string> restored;

        bool IsClean() const noexcept { return leaks.empty() && restored.empty(); }
    };

    // Puts every piece of process-wide libHttpClient state the unit tests touch back to its library
    // default:
    //  - if libHttpClient is still initialized (including nested HCInitialize calls): clears mocks and
    //    runs HCCleanupAsync until it is uninitialized, waiting at most cleanupTimeoutMs per attempt
    //  - perform callback (HCSetHttpCallPerformFunction) and WebSocket callbacks (HCSetWebSocketFunctions)
    //  - HC_CALL test hooks: perform-start, clock and perform/retry-delay (RetryTests)
    //  - memory hooks (HCMemSetFunctions), after verifying LeakCheckAllocator balance if it is installed.
    //    Left alone if cleanup timed out, because the abandoned cleanup still frees through them.
    //  - tracing: unbalanced HCTraceInit, client callbacks, platform callbacks, trace level,
    //    trace-to-debugger and ETW
    //  - process-wide request limit (HCSettingsSetGlobalRequestLimit)
    // Settings stored on the http_singleton (default timeouts, retry delay, timeout window, retry
    // allowed, mocks, routed handlers) are reset by the cleanup itself.
    static ResetResult ResetLibraryState(uint32_t cleanupTimeoutMs = c_defaultCleanupTimeoutMs) noexcept;

    // Per-test hooks used by DEFINE_TEST_CLASS_PROPS. SetUpTest forces a clean state and fails only
    // if it cannot get there; anything a previous test left behind is logged, not failed, so one
    // broken test does not fail the next one. TearDownTest never throws; pass its result to
    // ReportTearDown (after any class-specific cleanup) to fail the test if it leaked.
    static bool SetUpTest();
    static ResetResult TearDownTest() noexcept;
    static bool ReportTearDown(ResetResult const& result);

    // Records that a VERIFY failed in the current test. ReportTearDown then only logs leaks instead of
    // failing the test again: with TE a failure in the per-test cleanup replaces the test's own failure
    // message, and a test that failed part way through always "leaks" (that is what the cleanup is
    // for). The TE VERIFY macros call this through LHC_TRACK_VERIFY.
    static void NoteVerifyFailure() noexcept;

    static constexpr uint32_t c_defaultCleanupTimeoutMs = 10000;

private:
    static std::atomic<bool> s_verifyFailed;
};

// Notes a VERIFY failure when the check it guards exits by throwing (TE asserts throw).
class VerifyFailureTracker
{
public:
    VerifyFailureTracker() noexcept = default;
    VerifyFailureTracker(VerifyFailureTracker const&) = delete;
    VerifyFailureTracker& operator=(VerifyFailureTracker const&) = delete;

    ~VerifyFailureTracker()
    {
        if (!m_completed)
        {
            TestEnvironment::NoteVerifyFailure();
        }
    }

    void Completed() noexcept { m_completed = true; }

private:
    bool m_completed{ false };
};

#define LHC_TRACK_VERIFY(check) \
    do \
    { \
        ::xbox::httpclienttest::VerifyFailureTracker lhcVerifyTracker; \
        check; \
        lhcVerifyTracker.Completed(); \
    } while (false)

// Counting allocator for leak checks, installed through HCMemSetFunctions.
//
// Install it before HCInitialize. After HCCleanup every allocation libHttpClient made through it
// must have been freed; use WaitForBalance() (frees can land on a thread pool thread just after the
// operation that triggered them completes) or let the per-test cleanup verify it. The per-test
// cleanup also uninstalls it, so a failing test cannot leave it installed.
//
// It allocates with malloc/free, exactly like the default memory functions, so a block allocated
// through it and freed after it was uninstalled (or the other way around) is still freed correctly;
// only the counts are affected.
class LeakCheckAllocator
{
public:
    // Fails with E_HC_ALREADY_INITIALISED if libHttpClient is initialized. Resets the counters.
    static HRESULT Install() noexcept;

    // Restores the default memory functions. Fails if libHttpClient is initialized.
    static HRESULT Uninstall() noexcept;

    // True if HCMemGetFunctions currently returns this allocator's functions.
    static bool IsInstalled() noexcept;

    static uint64_t AllocCount() noexcept;
    static uint64_t FreeCount() noexcept;
    static int64_t OutstandingAllocations() noexcept;

    // Waits until every allocation has been freed. Returns false on timeout.
    static bool WaitForBalance(uint32_t timeoutMs = 2000) noexcept;

private:
    static _Ret_maybenull_ _Post_writable_byte_size_(size) void* STDAPIVCALLTYPE Alloc(
        _In_ size_t size,
        _In_ HCMemoryType memoryType
    ) noexcept;

    static void STDAPIVCALLTYPE Free(
        _In_ _Post_invalid_ void* pointer,
        _In_ HCMemoryType memoryType
    ) noexcept;

    static std::atomic<uint64_t> s_allocCount;
    static std::atomic<uint64_t> s_freeCount;

    // Blocks handed out since Install(). Only frees of these are counted: libHttpClient can free a
    // block it allocated through the previous (default) functions after Install() -- e.g. task queue
    // timer state from an earlier test that runs down on the timer thread -- and counting that free
    // would push the balance negative (a spurious "leak") or hide a real leak.
    static std::mutex s_blocksLock;
    static std::unordered_set<void*> s_blocks;
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
