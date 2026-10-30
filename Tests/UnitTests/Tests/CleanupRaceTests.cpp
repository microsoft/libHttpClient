// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Reproductions for the cleanup/request-kickoff races reported in
//   https://github.com/microsoft/libHttpClient/issues/1018
//   https://github.com/microsoft/libHttpClient/issues/1019
//   https://github.com/microsoft/libHttpClient/issues/1056
//   https://github.com/microsoft/libHttpClient/issues/1059
//
// These races are extremely narrow in the wild. These tests force the exact reported interleavings
// deterministically instead of relying on stress, and the crash reproductions convert the reported
// crash (an access violation) into a normal test failure so a regression reports as a failed
// assertion rather than tearing down the test host.
//

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "utils.h"
#include "PumpedTaskQueue.h"
#include <httpClient/httpProvider.h>
#ifndef HC_NOWEBSOCKETS
#include "Platform/ExternalWebSocketProvider.h"
#endif

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
#if HC_PLATFORM_IS_MICROSOFT
    static size_t PageAlignedSize(size_t size) noexcept
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        size_t const granularity = info.dwPageSize;
        size_t const usable = size == 0 ? 1 : size;
        return ((usable + granularity - 1) / granularity) * granularity;
    }
#endif

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

#if HC_PLATFORM_IS_MICROSOFT
        void* block = VirtualAlloc(nullptr, PageAlignedSize(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
        // No guard pages off Windows: armed blocks are ordinary heap blocks, so a use-after-free is
        // only caught when the test binary runs under AddressSanitizer (the Linux sanitizer leg).
        void* block = malloc(size == 0 ? 1 : size);
#endif
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
#if !HC_PLATFORM_IS_MICROSOFT
            m_blocks.erase(pointer);
#endif
        }

#if HC_PLATFORM_IS_MICROSOFT
        // Quarantine: keep the reservation so the address is never recycled, and make every
        // subsequent access fault.
        DWORD previousProtection{ 0 };
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(pointer, &info, sizeof(info)) == sizeof(info))
        {
            VirtualProtect(pointer, info.RegionSize, PAGE_NOACCESS, &previousProtection);
        }
#else
        free(pointer);
#endif
    }

    std::mutex m_mutex;
    std::set<void*> m_blocks;
    void* m_watched{ nullptr };
    bool m_watchedFreed{ false };
    bool m_armed{ false };
};

// SEH trampolines. These are deliberately free of C++ objects so __try/__except is legal, and they
// convert the reported access violation into a bool the tests can assert on.
#if HC_PLATFORM_IS_MICROSOFT

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

#else // No SEH: a fault (or an AddressSanitizer report) terminates the test process instead.

bool CleanupAsyncFaulted(XAsyncBlock* cleanupAsyncBlock, HRESULT* hr) noexcept
{
    *hr = HCCleanupAsync(cleanupAsyncBlock);
    return false;
}

bool DispatchFaulted(XTaskQueueHandle queue, XTaskQueuePort port, bool* dispatched) noexcept
{
    *dispatched = XTaskQueueDispatch(queue, port, 0);
    return false;
}

#endif

// Pumps both ports of a manual-dispatch queue until the async block leaves E_PENDING or the
// iteration budget runs out. Returns the final status.
HRESULT PumpUntilComplete(XTaskQueueHandle queue, XAsyncBlock* async, XTaskQueueHandle otherQueue = nullptr) noexcept
{
    for (int i = 0; i < 500 && XAsyncGetStatus(async, false) == E_PENDING; ++i)
    {
        bool dispatched = XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
        dispatched |= XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
        if (otherQueue)
        {
            dispatched |= XTaskQueueDispatch(otherQueue, XTaskQueuePort::Work, 0);
            dispatched |= XTaskQueueDispatch(otherQueue, XTaskQueuePort::Completion, 0);
        }
        if (!dispatched)
        {
            Sleep(10);
        }
    }
    return XAsyncGetStatus(async, false);
}

// Dispatches a manual-dispatch queue until it has stayed idle for a while, so work queued behind a
// completed operation (for example a terminated perform queue running down) is not stranded when
// the queue is closed.
void DrainQueue(XTaskQueueHandle queue) noexcept
{
    for (int idle = 0, i = 0; idle < 5 && i < 500; ++i)
    {
        bool dispatched = XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
        dispatched |= XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
        if (dispatched)
        {
            idle = 0;
        }
        else
        {
            ++idle;
            Sleep(10);
        }
    }
}

#ifndef HC_NOWEBSOCKETS
// WebSocket provider whose connect operation completes, but whose result cannot be retrieved. This is
// what the websocketpp provider reports once HCCleanupAsync has detached the http_singleton: its
// GetResult op looks its context up through the singleton and returns E_HC_NOT_INITIALISED.
HRESULT CALLBACK ResultUnavailableConnect(
    _In_z_ const char* /*uri*/,
    _In_z_ const char* /*subProtocol*/,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv /*env*/)
{
    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                return S_OK;
            case XAsyncOp::GetResult:
                return E_HC_NOT_INITIALISED;
            default:
                return S_OK;
            }
        });
}

HRESULT CALLBACK UnexpectedSend(HCWebsocketHandle, const char*, XAsyncBlock*, void*)
{
    return E_UNEXPECTED;
}

HRESULT CALLBACK UnexpectedSendBinary(HCWebsocketHandle, const uint8_t*, uint32_t, XAsyncBlock*, void*)
{
    return E_UNEXPECTED;
}

HRESULT CALLBACK UnexpectedDisconnect(HCWebsocketHandle, HCWebSocketCloseStatus, void*)
{
    return E_UNEXPECTED;
}

// WebSocket provider that connects, sends and closes synchronously, recording what it was handed.
struct FakeWebSocketProviderState
{
    HCWebsocketHandle providerHandle{ nullptr };
    HCWebsocketHandle lastCallHandle{ nullptr };
    uint32_t sends{ 0 };
    uint32_t disconnects{ 0 };
};
FakeWebSocketProviderState g_fakeWebSocket{};

HRESULT CompleteImmediately(XAsyncBlock* asyncBlock) noexcept
{
    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data)
    {
        if (op == XAsyncOp::Begin)
        {
            XAsyncComplete(data->async, S_OK, 0);
        }
        return S_OK;
    });
}

HRESULT CALLBACK FakeConnect(
    _In_z_ const char* /*uri*/,
    _In_z_ const char* /*subProtocol*/,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv /*env*/)
{
    g_fakeWebSocket.providerHandle = websocket;
    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                return S_OK;
            case XAsyncOp::GetResult:
            {
                auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
                *result = WebSocketCompletionResult{};
                result->websocket = static_cast<HCWebsocketHandle>(data->context);
                return S_OK;
            }
            default:
                return S_OK;
            }
        });
}

HRESULT CALLBACK FakeSend(HCWebsocketHandle websocket, const char*, XAsyncBlock* asyncBlock, void*)
{
    ++g_fakeWebSocket.sends;
    g_fakeWebSocket.lastCallHandle = websocket;
    return CompleteImmediately(asyncBlock);
}

HRESULT CALLBACK FakeSendBinary(HCWebsocketHandle websocket, const uint8_t*, uint32_t, XAsyncBlock* asyncBlock, void*)
{
    ++g_fakeWebSocket.sends;
    g_fakeWebSocket.lastCallHandle = websocket;
    return CompleteImmediately(asyncBlock);
}

// Reports the close the way a provider does: through the close function libHttpClient gave it.
HRESULT ReportProviderClose(HCWebsocketHandle providerHandle, HCWebSocketCloseStatus status) noexcept
{
    HCWebSocketCloseEventFunction closeFunc{ nullptr };
    void* closeContext{ nullptr };
    RETURN_IF_FAILED(HCWebSocketGetEventFunctions(providerHandle, nullptr, nullptr, &closeFunc, &closeContext));
    closeFunc(providerHandle, status, closeContext);
    return S_OK;
}

HRESULT CALLBACK FakeDisconnect(HCWebsocketHandle websocket, HCWebSocketCloseStatus status, void*)
{
    ++g_fakeWebSocket.disconnects;
    g_fakeWebSocket.lastCallHandle = websocket;
    return ReportProviderClose(websocket, status);
}

enum class ProviderCall { Send, SendBinary, Disconnect };

#if HC_PLATFORM_IS_MICROSOFT
__declspec(noinline) bool ProviderCallFaulted(ProviderCall call, HCWebsocketHandle websocket, XAsyncBlock* async, HRESULT* hr) noexcept
{
    static uint8_t const payload[]{ 1, 2, 3 };
    __try
    {
        switch (call)
        {
        case ProviderCall::Send: *hr = HCWebSocketSendMessageAsync(websocket, "message", async); break;
        case ProviderCall::SendBinary: *hr = HCWebSocketSendBinaryMessageAsync(websocket, payload, sizeof(payload), async); break;
        case ProviderCall::Disconnect: *hr = HCWebSocketDisconnect(websocket); break;
        }
        return false;
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    {
        return true;
    }
}
#else
bool ProviderCallFaulted(ProviderCall call, HCWebsocketHandle websocket, XAsyncBlock* async, HRESULT* hr) noexcept
{
    static uint8_t const payload[]{ 1, 2, 3 };
    switch (call)
    {
    case ProviderCall::Send: *hr = HCWebSocketSendMessageAsync(websocket, "message", async); break;
    case ProviderCall::SendBinary: *hr = HCWebSocketSendBinaryMessageAsync(websocket, payload, sizeof(payload), async); break;
    case ProviderCall::Disconnect: *hr = HCWebSocketDisconnect(websocket); break;
    }
    return false;
}
#endif

// Connects a WebSocket through the fake provider. Returns the client handle, or nullptr on failure.
HCWebsocketHandle ConnectFakeWebSocket() noexcept
{
    HCWebsocketHandle websocket{ nullptr };
    if (FAILED(HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr)))
    {
        return nullptr;
    }

    XAsyncBlock connectAsync{};
    WebSocketCompletionResult result{};
    if (FAILED(HCWebSocketConnectAsync("ws://127.0.0.1/socket", "", websocket, &connectAsync)) ||
        FAILED(XAsyncGetStatus(&connectAsync, true)) ||
        FAILED(HCGetWebSocketConnectResult(&connectAsync, &result)) ||
        FAILED(result.errorCode) ||
        g_fakeWebSocket.providerHandle == nullptr)
    {
        HCWebSocketCloseHandle(websocket);
        return nullptr;
    }
    return websocket;
}
#endif

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
        VERIFY_SUCCEEDED(HCMockCallCloseHandle(mock)); // HCMockAddMock keeps its own reference

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
        VERIFY_SUCCEEDED(HCMockCallCloseHandle(mock)); // HCMockAddMock keeps its own reference

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

    // ------------------------------------------------------------------------------------------
    // Issue #1059
    //
    // NetworkState tracks a perform in m_activeHttpRequests before HC_CALL::PerformAsync has begun
    // the perform's internal XAsyncBlock, so cleanup can snapshot the request and XAsyncCancel it
    // inside that window. The Cancel op forwarded XAsyncCancel straight to the internal block, which
    // is not in a usable state yet: either it has no async state (the cancel is silently dropped and
    // the request runs to completion anyway), or XAsyncBegin has published its state but not yet set
    // its provider (XAsyncCancel calls a null provider and crashes, as reported). The cancel must be
    // held until the internal block has been begun, and then forwarded.
    //
    // The test parks a perform after admission and before the internal block is begun, and issues
    // the same XAsyncCancel on the client block that NetworkState::CleanupAsyncProvider issues. It
    // cancels directly rather than through HCCleanupAsync because cleanup also detaches the
    // singleton, which makes the resumed perform fail on its own and would hide a dropped cancel.
    // ------------------------------------------------------------------------------------------
    DEFINE_TEST_CASE(TestCancelBeforeInternalPerformBeginIsNotLost)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCancelBeforeInternalPerformBeginIsNotLost);

        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        constexpr char mockUrl[]{ "www.bing.com" };
        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 200));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));
        VERIFY_SUCCEEDED(HCMockCallCloseHandle(mock)); // HCMockAddMock keeps its own reference

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));

        // Manual queue: nothing runs unless the test dispatches it, so the mock cannot complete the
        // request before a forwarded cancel has had the chance to terminate it.
        XTaskQueueHandle performQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &performQueue));

        struct Gate
        {
            Event parked;
            Event release;
            std::atomic<bool> used{ false };
        } gate;

        NetworkState::SetHttpPerformAdmittedTestHook([](void* context)
        {
            Gate* g{ static_cast<Gate*>(context) };
            bool expected{ false };
            if (!g->used.compare_exchange_strong(expected, true))
            {
                return;
            }
            g->parked.Set();
            g->release.Wait(c_waitTimeoutMs);
        }, &gate);

        XAsyncBlock performAsyncBlock{ performQueue };

        HRESULT performHr{ E_FAIL };
        std::thread performThread{ [&]()
        {
            performHr = HCHttpCallPerformAsync(call, &performAsyncBlock);
        } };

        bool const parked = gate.parked.Wait(c_waitTimeoutMs);
        bool const cancelable = parked && get_http_singleton()->m_networkState->CanCleanupCancelHttpRequest(&performAsyncBlock);

        // What cleanup does to every request in its snapshot.
        if (parked)
        {
            XAsyncCancel(&performAsyncBlock);
        }

        gate.release.Set();
        performThread.join();
        NetworkState::SetHttpPerformAdmittedTestHook(nullptr, nullptr);

        HRESULT const performStatus = PumpUntilComplete(performQueue, &performAsyncBlock);

        HCHttpCallCloseHandle(call);
        DrainQueue(performQueue);
        XTaskQueueCloseHandle(performQueue);
        HCCleanup();

        VERIFY_IS_TRUE(parked);
        VERIFY_IS_TRUE(cancelable); // the parked request is one cleanup would snapshot and cancel
        VERIFY_SUCCEEDED(performHr);

        // Pre-fix the cancel is dropped and the mock request completes successfully.
        VERIFY_ARE_EQUAL(E_ABORT, performStatus);
    }

#ifndef HC_NOWEBSOCKETS
    // ------------------------------------------------------------------------------------------
    // Issue #1056
    //
    // NetworkState::WebSocketConnectComplete stored the result of HCGetWebSocketConnectResult in hr
    // and then overwrote it with the result of XAsyncSchedule when that connect was the last
    // operation cleanup was waiting for. A connect whose result could not be retrieved was then
    // reported to the client as S_OK.
    // ------------------------------------------------------------------------------------------
    DEFINE_TEST_CASE(TestWebSocketConnectFailureDuringCleanupIsReported)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestWebSocketConnectFailureDuringCleanupIsReported);

        VERIFY_SUCCEEDED(HCSetWebSocketFunctions(ResultUnavailableConnect, UnexpectedSend, UnexpectedSendBinary, UnexpectedDisconnect, nullptr));
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        XTaskQueueHandle connectQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &connectQueue));
        XTaskQueueHandle cleanupQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &cleanupQueue));

        HCWebsocketHandle websocket{ nullptr };
        VERIFY_SUCCEEDED(HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));

        // The connect is admitted and its provider completes, but its completion stays queued.
        XAsyncBlock connectAsyncBlock{ connectQueue };
        VERIFY_SUCCEEDED(HCWebSocketConnectAsync("ws://127.0.0.1/socket", "", websocket, &connectAsyncBlock));

        // Cleanup begins while the connect is still pending, so cleanup waits for it.
        XAsyncBlock cleanupAsyncBlock{ cleanupQueue };
        VERIFY_SUCCEEDED(HCCleanupAsync(&cleanupAsyncBlock));

        // Completing the connect is what lets cleanup proceed.
        HRESULT const connectStatus = PumpUntilComplete(connectQueue, &connectAsyncBlock);
        WebSocketCompletionResult result{};
        HRESULT const resultHr = HCGetWebSocketConnectResult(&connectAsyncBlock, &result);

        VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));
        HRESULT const cleanupStatus = PumpUntilComplete(cleanupQueue, &cleanupAsyncBlock, connectQueue);

        DrainQueue(connectQueue);
        DrainQueue(cleanupQueue);
        XTaskQueueCloseHandle(connectQueue);
        XTaskQueueCloseHandle(cleanupQueue);

        // Pre-fix both report S_OK: the connect failure is replaced by XAsyncSchedule's result.
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, connectStatus);
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, resultHr);
        VERIFY_SUCCEEDED(cleanupStatus);
    }

    // ------------------------------------------------------------------------------------------
    // Send / disconnect racing the provider's close (follow-up to #1057).
    //
    // WebSocket::SendAsync, SendBinaryAsync and Disconnect checked that the socket was connected
    // and then read m_providerContext without holding the state lock, handing the provider the raw
    // observer it owns. A close reported by the provider in between (WebSocket::CloseFunc) frees
    // that context, and the pointer was never cleared, so the call went on through freed memory.
    // The test reports the provider's close at exactly that point and makes the use-after-free fault.
    // ------------------------------------------------------------------------------------------
    DEFINE_TEST_CASE(TestProviderCallRacingCloseDoesNotUseFreedProviderContext)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestProviderCallRacingCloseDoesNotUseFreedProviderContext);

        VERIFY_SUCCEEDED(GuardHeap::Install());
        GuardHeap::Get().Arm();

        struct Gate
        {
            HCWebsocketHandle providerHandle{ nullptr };
            bool fired{ false };
        };

        for (ProviderCall call : { ProviderCall::Send, ProviderCall::SendBinary, ProviderCall::Disconnect })
        {
            g_fakeWebSocket = FakeWebSocketProviderState{};
            VERIFY_SUCCEEDED(HCSetWebSocketFunctions(FakeConnect, FakeSend, FakeSendBinary, FakeDisconnect, nullptr));
            VERIFY_SUCCEEDED(HCInitialize(nullptr));

            HCWebsocketHandle websocket = ConnectFakeWebSocket();
            VERIFY_IS_NOT_NULL(websocket);

            Gate gate{ g_fakeWebSocket.providerHandle };
            WebSocket::SetProviderCallTestHook([](void* context)
            {
                Gate* g{ static_cast<Gate*>(context) };
                if (!g->fired)
                {
                    g->fired = true;
                    ReportProviderClose(g->providerHandle, HCWebSocketCloseStatus::GoingAway);
                }
            }, &gate);

            XAsyncBlock callAsync{};
            HRESULT callHr{ E_FAIL };
            bool const faulted = ProviderCallFaulted(call, websocket, &callAsync, &callHr);
            WebSocket::SetProviderCallTestHook(nullptr, nullptr);

            if (faulted)
            {
                // The library is left mid-call; don't try to tear it down.
                GuardHeap::Get().Disarm();
                VERIFY_IS_FALSE(faulted);
                return;
            }

            HRESULT callStatus{ callHr };
            if (call != ProviderCall::Disconnect && SUCCEEDED(callHr))
            {
                callStatus = XAsyncGetStatus(&callAsync, true);
            }

            HCWebsocketHandle const providerHandle{ g_fakeWebSocket.providerHandle };
            HCWebsocketHandle const handedToProvider{ g_fakeWebSocket.lastCallHandle };
            VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));
            HCCleanup();
            xbox::httpclient::ExternalWebSocketProvider::Get().ClearCallbacks();

            VERIFY_IS_TRUE(gate.fired);
            VERIFY_SUCCEEDED(callStatus);
            // The call was admitted before the close, so the provider gets it with a live handle.
            VERIFY_IS_TRUE(handedToProvider == providerHandle);
        }

        GuardHeap::Get().Disarm();
    }

    // ------------------------------------------------------------------------------------------
    // Cleanup racing a client disconnect (crash in WebSocket::Disconnect from CleanupAsyncProvider).
    //
    // CleanupAsyncProvider snapshotted m_connectedWebSockets as raw ActiveWebSocketContext pointers,
    // released m_mutex, and then disconnected each one through the snapshot. A close that finished in
    // between (here the client's own HCWebSocketDisconnect) frees the context in WebSocketClosed, and
    // once the client closes its handle the WebSocket itself is freed too, so cleanup called
    // Disconnect through freed memory.
    // ------------------------------------------------------------------------------------------
    DEFINE_TEST_CASE(TestCleanupDisconnectRacingCloseDoesNotUseFreedWebSocket)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCleanupDisconnectRacingCloseDoesNotUseFreedWebSocket);

        VERIFY_SUCCEEDED(GuardHeap::Install());
        GuardHeap::Get().Arm();

        g_fakeWebSocket = FakeWebSocketProviderState{};
        VERIFY_SUCCEEDED(HCSetWebSocketFunctions(FakeConnect, FakeSend, FakeSendBinary, FakeDisconnect, nullptr));
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        HCWebsocketHandle websocket = ConnectFakeWebSocket();
        VERIFY_IS_NOT_NULL(websocket);

        struct Gate
        {
            HCWebsocketHandle websocket{ nullptr };
            bool fired{ false };
            HRESULT disconnectHr{ E_FAIL };
        } gate{ websocket };

        NetworkState::SetCleanupSnapshotTestHook([](void* context)
        {
            Gate* g{ static_cast<Gate*>(context) };
            if (!g->fired)
            {
                g->fired = true;
                g->disconnectHr = HCWebSocketDisconnect(g->websocket);
                HCWebSocketCloseHandle(g->websocket);
            }
        }, &gate);

        XTaskQueueHandle cleanupQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &cleanupQueue));
        XAsyncBlock cleanupAsyncBlock{ cleanupQueue };

        HRESULT cleanupHr{ E_FAIL };
        bool const faulted = CleanupAsyncFaulted(&cleanupAsyncBlock, &cleanupHr);
        NetworkState::SetCleanupSnapshotTestHook(nullptr, nullptr);

        if (faulted)
        {
            // Cleanup is left half run; abandon it rather than unwind it.
            GuardHeap::Get().Disarm();
            VERIFY_IS_FALSE(faulted);
            return;
        }

        HRESULT const cleanupStatus = SUCCEEDED(cleanupHr) ? PumpUntilComplete(cleanupQueue, &cleanupAsyncBlock) : cleanupHr;
        DrainQueue(cleanupQueue);
        XTaskQueueCloseHandle(cleanupQueue);
        xbox::httpclient::ExternalWebSocketProvider::Get().ClearCallbacks();
        GuardHeap::Get().Disarm();

        VERIFY_IS_TRUE(gate.fired);
        VERIFY_SUCCEEDED(gate.disconnectHr);
        VERIFY_SUCCEEDED(cleanupHr);
        VERIFY_SUCCEEDED(cleanupStatus);
    }
#endif
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
