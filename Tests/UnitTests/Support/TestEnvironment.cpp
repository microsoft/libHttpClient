// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "TestEnvironment.h"
#include "Global/global.h"
#include "HTTP/httpcall.h"
#include "Logger/trace_internal.h"
#include "Platform/ExternalHttpProvider.h"
#ifndef HC_NOWEBSOCKETS
#include "Platform/ExternalWebSocketProvider.h"
#endif
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

// Upper bound on HCInitialize nesting we will unwind; the library's use count is a uint8_t.
constexpr uint32_t c_maxCleanupAttempts = 256;

void LogMessage(std::string const& message)
{
#if defined(UNITTEST_PORTABLE)
    hcunittest::LogMessage(message);
#else
    std::wstring wide{ message.begin(), message.end() };
#ifdef USING_TAEF
    WEX::Logging::Log::Comment(wide.c_str());
#else
    Logger::WriteMessage(wide.c_str());
#endif
#endif
}

std::string Join(std::vector<std::string> const& items)
{
    std::string joined;
    for (auto const& item : items)
    {
        if (!joined.empty())
        {
            joined += "; ";
        }
        joined += item;
    }
    return joined;
}

// One HCCleanupAsync in flight. Owned jointly by the waiter and the completion callback so the waiter
// can give up after a timeout without leaving the callback with a dangling block or queue.
struct PendingCleanup
{
    XAsyncBlock async{};
    XTaskQueueHandle queue{ nullptr };
    std::atomic<bool> completed{ false };
    std::atomic<uint32_t> owners{ 2 };

    static void Release(PendingCleanup* pending) noexcept
    {
        if (--pending->owners == 0)
        {
            XTaskQueueCloseHandle(pending->queue);
            delete pending;
        }
    }

    static void CALLBACK Completed(XAsyncBlock* async) noexcept
    {
        auto pending = static_cast<PendingCleanup*>(async->context);
        pending->completed = true;
        Release(pending);
    }
};

// Runs one HCCleanupAsync on a private thread pool queue and waits up to timeoutMs for it. On a
// timeout the cleanup is abandoned: it keeps running and frees its own block and queue if it ever
// completes. The singleton is detached as soon as cleanup begins, so the next test can still
// initialize libHttpClient.
bool RunCleanup(uint32_t timeoutMs, std::string& error) noexcept
{
    auto pending = new (std::nothrow) PendingCleanup{};
    if (pending == nullptr)
    {
        error = "out of memory";
        return false;
    }

    HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &pending->queue);
    if (FAILED(hr))
    {
        delete pending;
        error = "XTaskQueueCreate for HCCleanupAsync failed";
        return false;
    }

    pending->async.queue = pending->queue;
    pending->async.context = pending;
    pending->async.callback = PendingCleanup::Completed;

    hr = HCCleanupAsync(&pending->async);
    if (FAILED(hr))
    {
        // The callback will never run.
        PendingCleanup::Release(pending);
        PendingCleanup::Release(pending);
        error = "HCCleanupAsync failed";
        return false;
    }

    bool completed = true;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pending->completed)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            completed = false;
            error = "HCCleanupAsync did not complete within " + std::to_string(timeoutMs) + " ms (an HTTP call or WebSocket is probably still pending on a queue nobody dispatches)";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    PendingCleanup::Release(pending);
    return completed;
}

} // namespace

TestEnvironment::ResetResult TestEnvironment::ResetLibraryState(uint32_t cleanupTimeoutMs) noexcept
{
    ResetResult result;
    bool cleanupAbandoned = false;

    try
    {
        // 1. Uninitialize libHttpClient. Mocks, routed handlers and every setting stored on the
        // singleton (default timeouts, retry delay, timeout window, retry allowed) go with it.
        if (HCIsInitialized())
        {
            result.leaks.push_back("libHttpClient was still initialized (missing HCCleanup)");
            HCMockClearMocks();

            for (uint32_t attempt = 0; attempt < c_maxCleanupAttempts && HCIsInitialized(); ++attempt)
            {
                std::string error;
                if (!RunCleanup(cleanupTimeoutMs, error))
                {
                    result.leaks.push_back(error);
                    cleanupAbandoned = true;
                    break;
                }
            }

            if (HCIsInitialized())
            {
                result.leaks.push_back("libHttpClient is still initialized after cleanup");
            }
        }

        // 2. Client hooks that live outside the singleton. There is no public API to remove them.
        HC_CALL::SetPerformStartTestHook(nullptr, nullptr);
        NetworkState::SetHttpPerformAdmittedTestHook(nullptr, nullptr);
        NetworkState::SetCleanupSnapshotTestHook(nullptr, nullptr);
#ifndef HC_NOWEBSOCKETS
        WebSocket::SetProviderCallTestHook(nullptr, nullptr);
#endif
        // RetryTests' clock and retry-delay seams (decision 3): a test that fails before restoring
        // them must not leave later tests with a fake clock or intercepted retry delays.
        HC_CALL::SetClockTestHook(nullptr, nullptr);
        HC_CALL::SetPerformDelayTestHook(nullptr, nullptr);

        if (ExternalHttpProvider::Get().HasCallback())
        {
            result.restored.push_back("HCSetHttpCallPerformFunction callback");
            ExternalHttpProvider::Get().ClearCallback();
        }

#ifndef HC_NOWEBSOCKETS
        if (ExternalWebSocketProvider::Get().HasCallbacks())
        {
            result.restored.push_back("HCSetWebSocketFunctions callbacks");
            ExternalWebSocketProvider::Get().ClearCallbacks();
        }
#endif

        // 3. Process-wide request limit (deliberately survives HCCleanup).
        uint32_t const requestLimitBefore = GetGlobalRequestLimit();
        SetGlobalRequestLimit(0);
        if (GetGlobalRequestLimit() != requestLimitBefore)
        {
            result.restored.push_back("global request limit " + std::to_string(requestLimitBefore));
        }

        // 4. Tracing. HCInitialize/HCCleanup balance their own HCTraceInit/HCTraceCleanup, so once
        // libHttpClient is uninitialized any remaining tracing client is a leaked HCTraceInit.
        TraceState& traceState = GetTraceState();
        if (!cleanupAbandoned)
        {
            if (traceState.IsSetup())
            {
                result.leaks.push_back("HCTraceInit without matching HCTraceCleanup");
                for (uint32_t attempt = 0; attempt < c_maxCleanupAttempts && traceState.IsSetup(); ++attempt)
                {
                    HCTraceCleanup();
                }
            }

            if (traceState.HasClientCallbacks())
            {
                result.restored.push_back("HCTraceSetClientCallback callbacks");
                // TraceState::Cleanup is the only way to drop every client callback at once.
                HCTraceInit();
                HCTraceCleanup();
            }

            if (GetThreadIdInfo().callback != nullptr || GetWriteToDebuggerInfo().callback != nullptr)
            {
                result.restored.push_back("HCTraceSetPlatformCallbacks callbacks");
                HCTraceSetPlatformCallbacks(nullptr, nullptr, nullptr, nullptr);
            }
        }

        if (traceState.GetTraceToDebugger())
        {
            result.restored.push_back("HCTraceSetTraceToDebugger(true)");
            HCTraceSetTraceToDebugger(false);
        }

#if HC_PLATFORM_IS_MICROSOFT
        if (traceState.GetEtwEnabled())
        {
            result.restored.push_back("HCTraceSetEtwEnabled(true)");
            HCTraceSetEtwEnabled(false);
        }
#endif

        // Default verbosity for both areas, from HC_DEFINE_TRACE_AREA in global.cpp.
        HCTraceLevel traceLevel{ HCTraceLevel::Information };
        if (SUCCEEDED(HCSettingsGetTraceLevel(&traceLevel)) && traceLevel != HCTraceLevel::Information)
        {
            result.restored.push_back("trace level " + std::to_string(static_cast<int>(traceLevel)));
        }
        HCSettingsSetTraceLevel(HCTraceLevel::Information);

        // 5. Memory hooks, last, once nothing in libHttpClient can still allocate. If cleanup was
        // abandoned it may still free through the current hooks, so leave them in place.
        if (!cleanupAbandoned)
        {
            if (LeakCheckAllocator::IsInstalled())
            {
                result.restored.push_back("HCMemSetFunctions hooks (LeakCheckAllocator)");
                if (!LeakCheckAllocator::WaitForBalance())
                {
                    result.leaks.push_back(
                        std::to_string(LeakCheckAllocator::OutstandingAllocations()) + " libHttpClient allocation(s) not freed (" +
                        std::to_string(LeakCheckAllocator::AllocCount()) + " allocs, " +
                        std::to_string(LeakCheckAllocator::FreeCount()) + " frees)");
                }
                LeakCheckAllocator::Uninstall();
            }

            HCMemAllocFunction allocBefore{ nullptr };
            HCMemFreeFunction freeBefore{ nullptr };
            HCMemGetFunctions(&allocBefore, &freeBefore);
            HCMemSetFunctions(nullptr, nullptr);
            HCMemAllocFunction allocDefault{ nullptr };
            HCMemFreeFunction freeDefault{ nullptr };
            HCMemGetFunctions(&allocDefault, &freeDefault);
            if (allocBefore != allocDefault || freeBefore != freeDefault)
            {
                result.restored.push_back("HCMemSetFunctions hooks");
            }
        }
    }
    catch (...)
    {
        result.leaks.push_back("exception while resetting libHttpClient state");
    }

    return result;
}

bool TestEnvironment::SetUpTest()
{
    s_verifyFailed = false;

    ResetResult result = ResetLibraryState();
    if (!result.IsClean())
    {
        // Something outside the per-test fixture (or a previous test whose cleanup was abandoned)
        // left state behind. It has been reset; this test starts clean, so do not fail it.
        auto all = result.leaks;
        all.insert(all.end(), result.restored.begin(), result.restored.end());
        LogMessage("TestEnvironment: reset state left before this test: " + Join(all));
    }

    if (HCIsInitialized())
    {
#ifdef USING_TAEF
        WEX::Logging::Log::Error(L"TestEnvironment: libHttpClient is still initialized after reset");
        return false;
#elif defined(UNITTEST_PORTABLE)
        hcunittest::Fail(__FILE__, __LINE__, "TestEnvironment: libHttpClient is still initialized after reset");
#else
        Assert::Fail(L"TestEnvironment: libHttpClient is still initialized after reset");
#endif
    }
    return true;
}

TestEnvironment::ResetResult TestEnvironment::TearDownTest() noexcept
{
    ResetResult result = ResetLibraryState();
    if (!result.restored.empty())
    {
        LogMessage("TestEnvironment: restored defaults after test: " + Join(result.restored));
    }
    if (!result.leaks.empty())
    {
        LogMessage("TestEnvironment: test leaked: " + Join(result.leaks));
    }
    return result;
}

bool TestEnvironment::ReportTearDown(ResetResult const& result)
{
    if (result.leaks.empty())
    {
        return true;
    }

#if defined(UNITTEST_PORTABLE)
    // The portable VERIFY macros record failures in the runner rather than calling NoteVerifyFailure.
    bool const alreadyFailed = s_verifyFailed || hcunittest::HasRecordedFailures();
#else
    bool const alreadyFailed = s_verifyFailed;
#endif
    if (alreadyFailed)
    {
        // The test already failed; its own failure is the root cause and must stay the reported one.
        LogMessage("TestEnvironment: not failing the test again for the leaks above; it already failed a VERIFY");
        return false;
    }

    std::string message = "Test did not clean up libHttpClient state: " + Join(result.leaks);
#ifdef USING_TAEF
    std::wstring wide{ message.begin(), message.end() };
    WEX::Logging::Log::Error(wide.c_str());
    return false;
#elif defined(UNITTEST_PORTABLE)
    hcunittest::Fail(__FILE__, __LINE__, message);
#else
    std::wstring wide{ message.begin(), message.end() };
    Assert::Fail(wide.c_str());
#endif
}

void TestEnvironment::NoteVerifyFailure() noexcept
{
    s_verifyFailed = true;
}

std::atomic<bool> TestEnvironment::s_verifyFailed{ false };

std::atomic<uint64_t> LeakCheckAllocator::s_allocCount{ 0 };
std::atomic<uint64_t> LeakCheckAllocator::s_freeCount{ 0 };
std::mutex LeakCheckAllocator::s_blocksLock;
std::unordered_set<void*> LeakCheckAllocator::s_blocks;

HRESULT LeakCheckAllocator::Install() noexcept
{
    if (HCIsInitialized())
    {
        return E_HC_ALREADY_INITIALISED;
    }

    {
        std::lock_guard<std::mutex> lock{ s_blocksLock };
        s_blocks.clear();
        s_allocCount = 0;
        s_freeCount = 0;
    }
    return HCMemSetFunctions(Alloc, Free);
}

HRESULT LeakCheckAllocator::Uninstall() noexcept
{
    return HCMemSetFunctions(nullptr, nullptr);
}

bool LeakCheckAllocator::IsInstalled() noexcept
{
    HCMemAllocFunction allocFunc{ nullptr };
    HCMemFreeFunction freeFunc{ nullptr };
    return SUCCEEDED(HCMemGetFunctions(&allocFunc, &freeFunc)) && allocFunc == Alloc && freeFunc == Free;
}

uint64_t LeakCheckAllocator::AllocCount() noexcept
{
    return s_allocCount.load();
}

uint64_t LeakCheckAllocator::FreeCount() noexcept
{
    return s_freeCount.load();
}

int64_t LeakCheckAllocator::OutstandingAllocations() noexcept
{
    // Read frees first so a concurrent alloc+free pair can only make the result look higher.
    uint64_t const frees = s_freeCount.load();
    uint64_t const allocs = s_allocCount.load();
    return static_cast<int64_t>(allocs) - static_cast<int64_t>(frees);
}

bool LeakCheckAllocator::WaitForBalance(uint32_t timeoutMs) noexcept
{
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (OutstandingAllocations() != 0)
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

_Ret_maybenull_ _Post_writable_byte_size_(size) void* STDAPIVCALLTYPE LeakCheckAllocator::Alloc(
    _In_ size_t size,
    _In_ HCMemoryType /*memoryType*/
) noexcept
{
    void* pointer = std::malloc(size);
    if (pointer != nullptr)
    {
        try
        {
            std::lock_guard<std::mutex> lock{ s_blocksLock };
            s_blocks.insert(pointer);
            ++s_allocCount;
        }
        catch (...)
        {
            std::free(pointer);
            return nullptr;
        }
    }
    return pointer;
}

void STDAPIVCALLTYPE LeakCheckAllocator::Free(
    _In_ _Post_invalid_ void* pointer,
    _In_ HCMemoryType /*memoryType*/
) noexcept
{
    if (pointer != nullptr)
    {
        {
            // A block allocated before Install() came from the default functions (malloc), so
            // std::free is still the right way to release it; it just isn't counted.
            std::lock_guard<std::mutex> lock{ s_blocksLock };
            if (s_blocks.erase(pointer) != 0)
            {
                ++s_freeCount;
            }
        }
        std::free(pointer);
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
