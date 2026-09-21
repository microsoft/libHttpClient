// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Regression guard for a PLM Quiesce timeout in the WinHTTP WebSocket provider.
//
// WinHttpProvider::ConnectAsync acquires the provider-wide m_lock and holds it across
// WinHttpConnection::WebSocketConnectAsync. That call runs XAsyncOp::Begin inline on the caller's
// thread, so WinHttpConnection::SendRequest -> WinHttpSendRequest executes with m_lock held. Any
// other caller that needs m_lock is blocked for as long as WinHTTP takes to return.
//
// On GDK that other caller is WinHttpProvider::Suspend(), invoked from the PLM
// AppStateChangedCallback. Suspend() must take m_lock just to set m_isSuspended before it reaches
// its bounded 2000ms CloseAllConnections drain, so the suspend budget never applies and the title
// misses Quiesce. This is the wait chain in the reported dump.
//
// Suspend() compiles only under HC_PLATFORM_GDK, so this repro uses a second WebSocket connect as
// the victim. HCWebSocketConnectAsync runs entirely inline on the caller's thread down into
// WinHttpProvider::GetHSession and WinHttpProvider::ConnectAsync, both of which take the same
// m_lock that Suspend() takes. (An HTTP call is not usable as the victim: HC_CALL::PerfomAsyncProvider
// hands the request to the work port via XTaskQueueSubmitDelayedCallback, so HCHttpCallPerformAsync
// returns before it ever reaches the lock.) Win32 and GDK share one WinHttpProvider instance for
// both HTTP and WebSockets, so the lock being contended is the same object in both cases: if an
// unrelated caller is stalled here, Suspend() is stalled identically on a console.
//
// WinHttpSendRequest is stalled deterministically via the import thunk in
// WinHttpSendRequestStall.cpp rather than through a real network, because libHttpClient's HTTPS
// sessions are async and only stall when WinHTTP does synchronous work on the calling thread. The
// stall duration is simulated; the lock discipline being measured is the product's own.
//
// Exit codes follow the Tests/StandaloneTests convention:
//   0 = PASSED  (m_lock was not held across the WinHTTP call)
//   1 = REPRO   (m_lock was held; an unrelated m_lock caller was stalled)
//   2 = FAILED  (harness error -- result is inconclusive)

#include <httpClient/httpClient.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace WinHttpStall
{
    void SetStall(unsigned long ms) noexcept;
    bool SendRequestWasEntered() noexcept;
}

namespace
{
    using namespace std::chrono_literals;
    using Clock = std::chrono::steady_clock;

    // Comfortably longer than the 2000ms budget WinHttpProvider::Suspend allows its
    // CloseAllConnections drain, and long enough to be unambiguous against scheduler noise.
    constexpr unsigned long c_sendRequestStallMs = 5000;

    // A healthy provider releases m_lock before calling into WinHTTP, so the victim returns in
    // single-digit milliseconds. A regressed provider blocks it for the remainder of the stall,
    // roughly c_sendRequestStallMs - c_victimDelay (~4500ms). The threshold sits between those two
    // populations rather than close to the healthy one: the only realistic false failure is a
    // heavily loaded CI agent descheduling the victim thread, so leaving a 2000ms cushion above
    // "healthy" costs nothing in sensitivity while making that essentially impossible.
    constexpr auto c_victimStallThreshold = 2000ms;

    // How long the victim waits before contending for m_lock, so the connect thread is reliably
    // inside the stalled WinHttpSendRequest first.
    constexpr auto c_victimDelay = 500ms;

    // Loopback discard port: the connect is refused immediately, so once the injected stall is over
    // the request fails fast and teardown does not wait on a real network timeout. The repro is
    // about lock hold time, not about the request succeeding.
    constexpr char c_uri[] = "wss://127.0.0.1:9/stall";

    long long Ms(Clock::duration d)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
    }

    struct Gate
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool signaled{ false };

        void Set()
        {
            {
                std::lock_guard<std::mutex> lock{ mutex };
                signaled = true;
            }
            cv.notify_all();
        }

        bool WaitFor(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock{ mutex };
            return cv.wait_for(lock, timeout, [this]() noexcept { return signaled; });
        }
    };

    int Cleanup(XTaskQueueHandle queue, HCWebsocketHandle websocket, int code)
    {
        if (websocket)
        {
            HCWebSocketCloseHandle(websocket);
        }
        if (queue)
        {
            XTaskQueueTerminate(queue, true, nullptr, nullptr);
            XTaskQueueCloseHandle(queue);
        }
        HCCleanup();
        return code;
    }
}

int main()
{
    // Unbuffered so diagnostics survive if the process dies mid-run.
    setvbuf(stdout, nullptr, _IONBF, 0);

    WinHttpStall::SetStall(c_sendRequestStallMs);

    HRESULT hr = HCInitialize(nullptr);
    if (FAILED(hr))
    {
        std::printf("[winhttp-provider-lock] FAILED: HCInitialize returned 0x%08x\n", static_cast<unsigned int>(hr));
        return 2;
    }

    XTaskQueueHandle queue{ nullptr };
    hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
    if (FAILED(hr))
    {
        std::printf("[winhttp-provider-lock] FAILED: XTaskQueueCreate returned 0x%08x\n", static_cast<unsigned int>(hr));
        return Cleanup(nullptr, nullptr, 2);
    }

    HCWebsocketHandle websocket{ nullptr };
    hr = HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr))
    {
        std::printf("[winhttp-provider-lock] FAILED: HCWebSocketCreate returned 0x%08x\n", static_cast<unsigned int>(hr));
        return Cleanup(queue, nullptr, 2);
    }

    Gate connectStarted;

    XAsyncBlock connectAsync{};
    connectAsync.queue = queue;

    // Time spent inside HCWebSocketConnectAsync itself. The whole chain down to WinHttpSendRequest
    // runs inline on this thread, and m_lock is held for the tail of it.
    std::atomic<long long> connectCallMs{ -1 };
    std::atomic<unsigned int> connectHr{ 0 };

    std::thread connectThread([&]()
    {
        connectStarted.Set();
        auto start = Clock::now();
        HRESULT connectResult = HCWebSocketConnectAsync(c_uri, "", websocket, &connectAsync);
        connectCallMs.store(Ms(Clock::now() - start), std::memory_order_release);
        connectHr.store(static_cast<unsigned int>(connectResult), std::memory_order_release);
    });

    if (!connectStarted.WaitFor(5000ms))
    {
        std::printf("[winhttp-provider-lock] FAILED: connect thread never started\n");
        connectThread.join();
        return Cleanup(queue, websocket, 2);
    }

    std::this_thread::sleep_for(c_victimDelay);

    if (!WinHttpStall::SendRequestWasEntered())
    {
        std::printf("[winhttp-provider-lock] FAILED: WinHttpSendRequest was never reached; the stall was not injected\n");
        connectThread.join();
        return Cleanup(queue, websocket, 2);
    }

    // Public API whose inline path takes WinHttpProvider::m_lock, exactly as Suspend() does.
    // The interposer arms only once, so this connect's own send is not stalled: essentially
    // everything measured here is time spent waiting for m_lock.
    HCWebsocketHandle victimSocket{ nullptr };
    HRESULT victimHr = HCWebSocketCreate(&victimSocket, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(victimHr))
    {
        std::printf("[winhttp-provider-lock] FAILED: victim HCWebSocketCreate returned 0x%08x\n", static_cast<unsigned int>(victimHr));
        connectThread.join();
        return Cleanup(queue, websocket, 2);
    }

    XAsyncBlock victimAsync{};
    victimAsync.queue = queue;

    auto victimStart = Clock::now();
    victimHr = HCWebSocketConnectAsync(c_uri, "", victimSocket, &victimAsync);
    long long victimMs = Ms(Clock::now() - victimStart);

    connectThread.join();
    long long connectMs = connectCallMs.load(std::memory_order_acquire);

    std::printf("[winhttp-provider-lock] injected WinHttpSendRequest stall: %lums\n", c_sendRequestStallMs);
    std::printf("[winhttp-provider-lock] HCWebSocketConnectAsync returned 0x%08x after %lldms\n",
        connectHr.load(std::memory_order_acquire), connectMs);
    std::printf("[winhttp-provider-lock] HCWebSocketConnectAsync (victim, stands in for Suspend) returned 0x%08x after %lldms\n",
        static_cast<unsigned int>(victimHr), victimMs);

    (void)XAsyncGetStatus(&connectAsync, true);
    (void)XAsyncGetStatus(&victimAsync, true);
    HCWebSocketCloseHandle(victimSocket);

    if (victimMs >= Ms(c_victimStallThreshold))
    {
        std::printf("[winhttp-provider-lock] REPRO: provider m_lock was held across the WinHTTP call for %lldms.\n", victimMs);
        std::printf("[winhttp-provider-lock]        On GDK this is WinHttpProvider::Suspend() blocked at winhttp_provider.cpp:811,\n");
        std::printf("[winhttp-provider-lock]        before it can reach its bounded 2000ms CloseAllConnections drain.\n");
        return Cleanup(queue, websocket, 1);
    }

    std::printf("[winhttp-provider-lock] PASSED: m_lock was released before the WinHTTP call (victim %lldms).\n", victimMs);
    return Cleanup(queue, websocket, 0);
}
