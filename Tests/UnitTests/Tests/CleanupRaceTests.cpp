// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Reproductions for the cleanup/request-kickoff races reported in
//   https://github.com/microsoft/libHttpClient/issues/1018
//   https://github.com/microsoft/libHttpClient/issues/1019
//
// Both races are extremely narrow in the wild. These tests force the exact reported interleavings
// deterministically instead of relying on stress, and both convert the reported crash (an access
// violation) into a normal test failure so a regression reports as a failed assertion rather than
// tearing down the test host.
//

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "PumpedTaskQueue.h"

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

// Scoped event wrapper so the SEH-bearing helpers below never need C++ unwinding.
class Event
{
public:
    Event() : m_handle{ CreateEvent(nullptr, TRUE, FALSE, nullptr) } {}
    ~Event() { if (m_handle) { CloseHandle(m_handle); } }
    Event(Event const&) = delete;
    Event& operator=(Event const&) = delete;

    HANDLE Get() const noexcept { return m_handle; }
    void Set() const noexcept { SetEvent(m_handle); }
    bool Wait(DWORD timeoutMs) const noexcept { return WaitForSingleObject(m_handle, timeoutMs) == WAIT_OBJECT_0; }

private:
    HANDLE m_handle;
};

constexpr DWORD c_waitTimeoutMs = 30000;

// ---------------------------------------------------------------------------------------------
// Guard-page allocator used as a use-after-free oracle.
//
// While armed, every libHttpClient allocation gets its own page-aligned region. Freeing does not
// release the region; it flips it to PAGE_NOACCESS and quarantines it forever. Any later read or
// write through a dangling libHttpClient pointer therefore raises an access violation at the exact
// instruction that touches the freed object, which the test catches and reports.
//
// The hooks stay installed for the life of the process and fall back to malloc/free while disarmed,
// so quarantined blocks are always routed back here and never handed to the CRT allocator.
// ---------------------------------------------------------------------------------------------
class GuardHeap
{
public:
    static GuardHeap& Get() noexcept
    {
        static GuardHeap s_instance;
        return s_instance;
    }

    // Installs the hooks. Must be called while libHttpClient is uninitialized.
    static HRESULT Install() noexcept
    {
        return HCMemSetFunctions(AllocHook, FreeHook);
    }

    void Arm() noexcept
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_armed = true;
    }

    void Disarm() noexcept
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_armed = false;
    }

    // Records a pointer whose free should be reported back to the test.
    void Watch(void* pointer) noexcept
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_watched = pointer;
        m_watchedFreed = false;
    }

    bool WatchedWasFreed() noexcept
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        return m_watchedFreed;
    }

private:
    static size_t PageAlignedSize(size_t size) noexcept
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        size_t const granularity = info.dwPageSize;
        size_t const usable = size == 0 ? 1 : size;
        return ((usable + granularity - 1) / granularity) * granularity;
    }

    static _Ret_maybenull_ void* STDAPIVCALLTYPE AllocHook(size_t size, HCMemoryType) noexcept
    {
        return Get().Alloc(size);
    }

    static void STDAPIVCALLTYPE FreeHook(_Post_invalid_ void* pointer, HCMemoryType) noexcept
    {
        Get().Free(pointer);
    }

    void* Alloc(size_t size) noexcept
    {
        {
            std::lock_guard<std::mutex> lock{ m_mutex };
            if (!m_armed)
            {
                return malloc(size);
            }
        }

        void* block = VirtualAlloc(nullptr, PageAlignedSize(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!block)
        {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock{ m_mutex };
        m_blocks.insert(block);
        return block;
    }

    void Free(void* pointer) noexcept
    {
        if (!pointer)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock{ m_mutex };
            if (m_blocks.find(pointer) == m_blocks.end())
            {
                // Allocated while disarmed; hand it back to the CRT.
                free(pointer);
                return;
            }

            if (pointer == m_watched)
            {
                m_watchedFreed = true;
            }
        }

        // Quarantine: keep the reservation so the address is never recycled, and make every
        // subsequent access fault.
        DWORD previousProtection{ 0 };
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(pointer, &info, sizeof(info)) == sizeof(info))
        {
            VirtualProtect(pointer, info.RegionSize, PAGE_NOACCESS, &previousProtection);
        }
    }

    std::mutex m_mutex;
    std::set<void*> m_blocks;
    void* m_watched{ nullptr };
    bool m_watchedFreed{ false };
    bool m_armed{ false };
};

// SEH trampolines. These are deliberately free of C++ objects so __try/__except is legal, and they
// convert the reported access violation into a bool the tests can assert on.

__declspec(noinline) bool CleanupAsyncFaulted(XAsyncBlock* cleanupAsyncBlock, HRESULT* hr) noexcept
{
    __try
    {
        *hr = HCCleanupAsync(cleanupAsyncBlock);
        return false;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return true;
    }
}

__declspec(noinline) bool DispatchFaulted(XTaskQueueHandle queue, XTaskQueuePort port, bool* dispatched) noexcept
{
    __try
    {
        *dispatched = XTaskQueueDispatch(queue, port, 0);
        return false;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return true;
    }
}

}

DEFINE_TEST_CLASS(CleanupRaceTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(CleanupRaceTests);

    // ------------------------------------------------------------------------------------------
    // Issue #1018
    //
    // NetworkState::HttpCallPerformAsyncProvider inserts a request into m_activeHttpRequests before
    // HC_CALL::PerformAsync has started, so cleanup can snapshot that request and cancel it while
    // HC_CALL::PerfomAsyncProvider's Begin op is still running. If the cancel lands before Begin
    // creates PerformContext::workQueue, the Cancel op calls XTaskQueueTerminate(nullptr, ...),
    // which dereferences a null task queue handle.
    //
    // The test parks a perform inside that exact window and then runs cleanup.
    // ------------------------------------------------------------------------------------------
    DEFINE_TEST_CASE(TestCleanupCancelDuringPerformStartupDoesNotCrash)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCleanupCancelDuringPerformStartupDoesNotCrash);

        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        constexpr char mockUrl[]{ "www.bing.com" };
        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 200));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));

        // The gate is heap allocated and deliberately leaked if the cancel path faults. Catching an
        // access violation leaves libHttpClient's cancel handshake half finished, so the parked
        // perform can never be released safely; leaking keeps its wait handle valid forever instead
        // of letting it wake onto a destroyed stack frame.
        struct Gate
        {
            Event parked;
            Event release;
            std::atomic<bool> used{ false };
        };
        Gate* gate{ new Gate() };

        // The pumped queue is also heap owned so it is not torn down (and joined) after a fault.
        PumpedTaskQueue* pumpedQueue{ new PumpedTaskQueue() };

        Event performComplete;
        Event cleanupComplete;

        // Park the very first perform inside PerfomAsyncProvider's Begin op, before the work queues
        // exist. Later performs (for example retries) must not re-enter the gate.
        HC_CALL::SetPerformStartTestHook([](void* context)
        {
            Gate* g{ static_cast<Gate*>(context) };
            bool expected{ false };
            if (!g->used.compare_exchange_strong(expected, true))
            {
                return;
            }
            g->parked.Set();
            g->release.Wait(c_waitTimeoutMs);
        }, gate);

        XAsyncBlock performAsyncBlock{ pumpedQueue->queue, &performComplete, [](XAsyncBlock* async)
        {
            static_cast<Event*>(async->context)->Set();
        } };

        XAsyncBlock cleanupAsyncBlock{ pumpedQueue->queue, &cleanupComplete, [](XAsyncBlock* async)
        {
            static_cast<Event*>(async->context)->Set();
        } };

        // Kick the perform off on another thread; it will stall inside the startup window.
        std::thread performThread{ [&]()
        {
            HCHttpCallPerformAsync(call, &performAsyncBlock);
        } };

        bool const parked = gate->parked.Wait(c_waitTimeoutMs);

        HRESULT cleanupHr{ E_FAIL };
        bool const faulted = parked ? CleanupAsyncFaulted(&cleanupAsyncBlock, &cleanupHr) : false;

        bool drained{ false };
        if (faulted)
        {
            // Pre-fix path: the cancel dereferenced a null work queue. Abandon the parked perform
            // and the queue rather than trying to unwind a half-canceled operation.
            performThread.detach();
        }
        else
        {
            gate->release.Set();
            performThread.join();

            if (SUCCEEDED(cleanupHr))
            {
                drained = cleanupComplete.Wait(c_waitTimeoutMs) && performComplete.Wait(c_waitTimeoutMs);
            }

            HC_CALL::SetPerformStartTestHook(nullptr, nullptr);
            HCHttpCallCloseHandle(call);
            delete pumpedQueue;
            delete gate;
        }

        VERIFY_IS_TRUE(parked); // perform must actually have reached the startup window

        // Pre-fix this faults inside XTaskQueueTerminate(nullptr, ...) via the cancel path.
        VERIFY_IS_FALSE(faulted);
        VERIFY_SUCCEEDED(cleanupHr);
        VERIFY_IS_TRUE(drained); // cleanup and the canceled perform must both finish
    }

    // ------------------------------------------------------------------------------------------
    // Issue #1019
    //
    // Once NetworkState cleanup has begun, a perform that reaches HttpCallPerformAsyncProvider's
    // Begin op is refused with E_HC_NOT_INITIALISED and is never inserted into m_activeHttpRequests.
    // Refusing it is correct, but the refused request's XAsyncOp::Cleanup still runs later (on the
    // completion port, after the client callback) and still dereferences NetworkState to take
    // m_mutex and call erase(). A refused request holds no singleton reference, so nothing keeps
    // the singleton -- and the NetworkState it owns -- alive until that deferred op runs.
    //
    // The test drives the reported ordering exactly: refuse a perform, let cleanup run to
    // completion (destroying NetworkState), and only then dispatch the refused request's deferred
    // cleanup. The guard-page heap turns the resulting use-after-free into a catchable fault.
    // ------------------------------------------------------------------------------------------
    DEFINE_TEST_CASE(TestRejectedPerformCleanupDoesNotTouchFreedNetworkState)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRejectedPerformCleanupDoesNotTouchFreedNetworkState);

        VERIFY_SUCCEEDED(GuardHeap::Install());
        GuardHeap::Get().Arm();

        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        // Two queues on purpose. The whole cleanup chain runs on cleanupQueue, while the refused
        // request completes on performQueue. libHttpClient's provider cleanup completes through the
        // completion port, so draining cleanup to the point where NetworkState is destroyed means
        // pumping both of cleanupQueue's ports -- and the refused request's deferred cleanup must
        // not be pumped along with it. Separate queues give that ordering exactly.
        XTaskQueueHandle cleanupQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &cleanupQueue));

        XTaskQueueHandle performQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &performQueue));

        constexpr char mockUrl[]{ "www.bing.com" };
        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 200));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));

        // Model an in-flight HCHttpCallPerformAsync caller: it has already passed the public
        // null-singleton check and holds a strong reference, but has not yet reached NetworkState.
        auto inFlightSingletonRef = get_http_singleton();
        VERIFY_IS_NOT_NULL(inFlightSingletonRef.get());

        NetworkState* networkState{ inFlightSingletonRef->m_networkState.get() };
        VERIFY_IS_NOT_NULL(networkState);
        GuardHeap::Get().Watch(networkState);

        // Cleanup begins: this detaches the singleton and sets NetworkState's m_cleanupStarted.
        XAsyncBlock cleanupAsyncBlock{ cleanupQueue };
        VERIFY_SUCCEEDED(HCCleanupAsync(&cleanupAsyncBlock));

        // The in-flight caller now reaches NetworkState and is refused.
        XAsyncBlock performAsyncBlock{ performQueue, nullptr, [](XAsyncBlock*) {} };
        VERIFY_SUCCEEDED(networkState->HttpCallPerformAsync(call, &performAsyncBlock));
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, XAsyncGetStatus(&performAsyncBlock, false));

        // Release the in-flight reference so the singleton's use_count gate can open.
        inFlightSingletonRef.reset();

        // Drain cleanup to completion. This destroys the singleton and, with it, NetworkState,
        // while the refused request's deferred cleanup is still parked on performQueue.
        bool networkStateFreed{ false };
        for (int i = 0; i < 300 && !networkStateFreed; ++i)
        {
            XTaskQueueDispatch(cleanupQueue, XTaskQueuePort::Work, 0);
            XTaskQueueDispatch(cleanupQueue, XTaskQueuePort::Completion, 0);
            networkStateFreed = GuardHeap::Get().WatchedWasFreed();
            if (!networkStateFreed)
            {
                Sleep(10);
            }
        }

        HRESULT const cleanupStatus = XAsyncGetStatus(&cleanupAsyncBlock, false);

        // Now run the refused request's deferred XAsyncOp::Cleanup.
        bool faulted{ false };
        if (networkStateFreed)
        {
            bool dispatched{ false };
            faulted = DispatchFaulted(performQueue, XTaskQueuePort::Completion, &dispatched);
        }

        // Drain anything still queued before tearing down, ignoring further faults so cleanup of
        // the test itself cannot mask the result.
        for (int i = 0; i < 50; ++i)
        {
            bool dispatched{ false };
            DispatchFaulted(performQueue, XTaskQueuePort::Completion, &dispatched);
            DispatchFaulted(performQueue, XTaskQueuePort::Work, &dispatched);
        }

        HCHttpCallCloseHandle(call);
        XTaskQueueCloseHandle(performQueue);
        XTaskQueueCloseHandle(cleanupQueue);
        GuardHeap::Get().Disarm();

        // The ordering under test must actually have occurred, otherwise this proves nothing.
        VERIFY_SUCCEEDED(cleanupStatus);
        VERIFY_IS_TRUE(networkStateFreed);

        // Pre-fix the deferred cleanup takes NetworkState::m_mutex on freed memory.
        VERIFY_IS_FALSE(faulted);
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
