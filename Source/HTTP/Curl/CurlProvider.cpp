#include "pch.h"
#include "CurlProvider.h"
#include "CurlEasyRequest.h"
#include "CurlDynamicLoader.h"

namespace xbox
{
namespace httpclient
{

HRESULT HrFromCurle(CURLcode c) noexcept
{
    switch (c)
    {
    case CURLcode::CURLE_OK: return S_OK;
    case CURLcode::CURLE_BAD_FUNCTION_ARGUMENT: assert(false); return E_INVALIDARG; // Indicates bad provider implementation
    default: return E_FAIL;
    }
}

HRESULT HrFromCurlm(CURLMcode c) noexcept
{
    switch (c)
    {
    case CURLMcode::CURLM_OK: return S_OK;
#if HC_PLATFORM == HC_PLATFORM_GDK
    case CURLMcode::CURLM_BAD_FUNCTION_ARGUMENT: assert(false); return E_INVALIDARG;
#elif defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7,69,0)
    case CURLMcode::CURLM_BAD_FUNCTION_ARGUMENT: assert(false); return E_INVALIDARG;
#endif
    default: return E_FAIL;
    }
}

Result<HC_UNIQUE_PTR<CurlProvider>> CurlProvider::Initialize()
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    // Initialize dynamic curl loader first
    auto& loader = CurlDynamicLoader::GetInstance();
    if (!loader.Initialize())
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlProvider::Initialize: Failed to load XCurl.dll");
        // Ensure the loader is cleaned up if initialization fails
        CurlDynamicLoader::DestroyInstance();
        return E_FAIL;
    }

    CURLcode initRes = CURL_CALL(curl_global_init)(CURL_GLOBAL_ALL);
    HRESULT initHr = HrFromCurle(initRes);
    if (FAILED(initHr))
    {
        // If curl init fails, unload XCurl and free the loader singleton
        CurlDynamicLoader::DestroyInstance();
        return initHr;
    }
#else
    CURLcode initRes = CURL_CALL(curl_global_init)(CURL_GLOBAL_ALL);
    RETURN_IF_FAILED(HrFromCurle(initRes));
#endif

    http_stl_allocator<CurlProvider> a{};
    auto provider = HC_UNIQUE_PTR<CurlProvider>{ new (a.allocate(1)) CurlProvider };

#if HC_PLATFORM == HC_PLATFORM_GDK
    // Mirror WinHttpProvider: subscribe to PLM app state so the provider can keep the curl
    // perform loop running while the title suspends. Without this the loop only advances when
    // the title dispatches its own task queue, and a title that parks that queue on suspend
    // leaves xCurl blocked in WaitForActiveHandles until the watchdog kills it (bug 63050439).
    HRESULT registerHr = RegisterAppStateChangeNotification(CurlProvider::AppStateChangedCallback, provider.get(), &provider->m_appStateChangedToken);
    if (FAILED(registerHr))
    {
        // Suspend handling is a resilience feature; failing to subscribe must not stop HTTP from
        // working, so log and continue rather than failing initialization.
        HC_TRACE_ERROR_HR(HTTPCLIENT, registerHr, "CurlProvider::Initialize: RegisterAppStateChangeNotification failed; suspend handling disabled");
        provider->m_appStateChangedToken = nullptr;
    }
#endif

    return std::move(provider);
}

CurlProvider::~CurlProvider()
{
    // Either CleanupAsync was never called or CurlProvider shouldn't be destroyed until it completes.
    assert(!m_cleanupTasksRemaining);

#if HC_PLATFORM == HC_PLATFORM_GDK
    if (m_appStateChangedToken)
    {
        UnregisterAppStateChangeNotification(m_appStateChangedToken);
        m_appStateChangedToken = nullptr;
    }
#endif

    if (m_multiCleanupQueue)
    {
        XTaskQueueCloseHandle(m_multiCleanupQueue);
    }

    // make sure XCurlMultis are cleaned up before curl_global_cleanup
    m_curlMultis.clear();

#if HC_PLATFORM == HC_PLATFORM_GDK
    if (CurlDynamicLoader::GetInstance().IsLoaded())
    {
        CURL_CALL(curl_global_cleanup)();
    }
    // Free the dynamic loader singleton (unloads XCurl.dll via its destructor)
    CurlDynamicLoader::DestroyInstance();
#else
    CURL_CALL(curl_global_cleanup)();
#endif
}

HRESULT CurlProvider::PerformAsync(HCCallHandle hcCall, XAsyncBlock* async) noexcept
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    // Check if curl is available before proceeding
    if (!CurlDynamicLoader::GetInstance().IsLoaded())
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlProvider::PerformAsync: XCurl.dll not available");
        return E_HC_XCURL_REQUIRED;
    }
#endif

    XTaskQueuePortHandle workPort{ nullptr };
    RETURN_IF_FAILED(XTaskQueueGetPort(async->queue, XTaskQueuePort::Work, &workPort));

    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlProvider::PerformAsync: HCCallHandle=%p, workPort=%p", hcCall, workPort);

    auto easyInitResult = CurlEasyRequest::Initialize(hcCall, async);
    RETURN_IF_FAILED(easyInitResult.hr);

    http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::httpclient::CurlMulti>>::iterator iter;
    {
        // CurlProvider::PerformAsync can be called simultaneously from multiple threads so we need to lock
        // to prevent unsafe access to m_curlMultis
        std::lock_guard<std::mutex> lock{ m_mutex };

        iter = m_curlMultis.find(workPort);
        if (iter == m_curlMultis.end())
        {
            auto multiInitResult = CurlMulti::Initialize(workPort);
            RETURN_IF_FAILED(multiInitResult.hr);

            iter = m_curlMultis.emplace(workPort, multiInitResult.ExtractPayload()).first;
        }

        auto& multi{ iter->second };
        RETURN_IF_FAILED(multi->AddRequest(easyInitResult.ExtractPayload()));
    }

    return S_OK;
}

HRESULT CurlProvider::CleanupAsync(XAsyncBlock* async) noexcept
{
    return XAsyncBegin(async, this, __FUNCTION__, __FUNCTION__, CleanupAsyncProvider);
}

HRESULT CALLBACK CurlProvider::CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data) noexcept
{
    switch (op)
    {
    case XAsyncOp::Begin:
    {
        CurlProvider* provider{ static_cast<CurlProvider*>(data->context) };

        // CleanupAsync should never be called more than once
        assert(provider->m_cleanupAsyncBlock == nullptr);
        provider->m_cleanupAsyncBlock = data->async;

        XTaskQueuePortHandle workPort{ nullptr };
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &provider->m_multiCleanupQueue));

        http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::httpclient::CurlMulti>> localCurlMultis;
        {
            std::lock_guard<std::mutex> lock{ provider->m_mutex };
            localCurlMultis = std::move(provider->m_curlMultis);
            provider->m_curlMultis.clear();

            // There is a race condition where the last CurlMulti::CleanupAsync task can complete before the cleanup loop is finished.
            // Because the loop condition relies on the provider being alive, we add an additional cleanup task, ensuring the provider
            // can never be destroyed until after the loop.
            provider->m_cleanupTasksRemaining = 1 + localCurlMultis.size();
        }

        XAsyncBlock multiCleanupAsyncBlock{ provider->m_multiCleanupQueue, provider, CurlProvider::MultiCleanupComplete, { 0 } };
        provider->m_multiCleanupAsyncBlocks = http_internal_vector<XAsyncBlock>(localCurlMultis.size(), multiCleanupAsyncBlock);

        size_t multiIndex{ 0 };
        bool cleanupComplete{ false };

        for (auto& pair : localCurlMultis)
        {
            HRESULT hr = CurlMulti::CleanupAsync(std::move(pair.second), &provider->m_multiCleanupAsyncBlocks[multiIndex++]);
            if (FAILED(hr))
            {
                // Continue cleanup if this fails, but we should expect 1 fewer MultiCleanupComplete callback
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlMulti::CleanupAsync failed, continuing cleanup");

                std::lock_guard<std::mutex> lock{ provider->m_mutex };
                --provider->m_cleanupTasksRemaining;
            }
        }

        {
            std::lock_guard<std::mutex> lock{ provider->m_mutex };
            HC_TRACE_VERBOSE(HTTPCLIENT, "CurlProvider::CleanupAsyncProvider, cleanupTasksRemaining=%llu", provider->m_cleanupTasksRemaining - 1);
            if (--provider->m_cleanupTasksRemaining == 0)
            {
                // If there are no pending pending multi cleanups, complete cleanup here
                cleanupComplete = true;
            }
        }

        if (cleanupComplete)
        {
            XAsyncComplete(data->async, S_OK, 0);
        }

        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK CurlProvider::MultiCleanupComplete(_Inout_ struct XAsyncBlock* asyncBlock) noexcept
{
    CurlProvider* provider{ static_cast<CurlProvider*>(asyncBlock->context) };

    std::unique_lock<std::mutex> lock{ provider->m_mutex };
    HC_TRACE_VERBOSE(HTTPCLIENT, "CurlProvider::MultiCleanupComplete, cleanupTasksRemaining=%llu", provider->m_cleanupTasksRemaining-1);

    if (--provider->m_cleanupTasksRemaining == 0)
    {
        // All CurlMultis have finished asyncCleanup. Destroy provider and complete provider's Cleanup XAsyncBlock
        XAsyncBlock* providerCleanupAsyncBlock{ provider->m_cleanupAsyncBlock };

        // Release lock before completing async operation, since CurlProvider could be destroyed anytime after XAsyncComplete is called
        lock.unlock();

        XAsyncComplete(providerCleanupAsyncBlock, S_OK, 0);
    }
}

#if HC_PLATFORM == HC_PLATFORM_GDK

// How long the provider will drive curl_multi_perform while suspending. The xCurl contract
// requires the multi consumer to keep performing across suspend so xCurl can quiesce its
// handles; this bounds that work so a stuck request can never hold the suspend open longer
// than the platform's own watchdog budget.
#define SUSPEND_DRAIN_TIMEOUT_MS 4000

void CurlProvider::Suspend() noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlProvider::Suspend");

    // The lock is held for the whole drain so a concurrent CleanupAsync cannot move and destroy
    // the CurlMultis while they are being performed. Completions are delivered through the
    // async block's task queue rather than inline, so no request completion can re-enter
    // PerformAsync on this thread while the lock is held. The drain is time-bounded, so the
    // worst case for a blocked caller is SUSPEND_DRAIN_TIMEOUT_MS per multi.
    std::lock_guard<std::mutex> lock{ m_mutex };

    if (m_isSuspended)
    {
        return;
    }
    m_isSuspended = true;

    for (auto& pair : m_curlMultis)
    {
        CurlMulti* multi = pair.second.get();
        if (!multi || multi->ActiveRequestCount() == 0)
        {
            continue;
        }

        HRESULT hr = multi->PerformUntilDrained(SUSPEND_DRAIN_TIMEOUT_MS);
        if (FAILED(hr))
        {
            HC_TRACE_WARNING_HR(HTTPCLIENT, hr, "CurlProvider::Suspend: CurlMulti did not fully drain before suspend");
        }
    }
}

void CurlProvider::Resume() noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "CurlProvider::Resume");

    std::lock_guard<std::mutex> lock{ m_mutex };
    m_isSuspended = false;
}

void CALLBACK CurlProvider::AppStateChangedCallback(BOOLEAN isSuspended, void* context)
{
    assert(context);
    auto provider = static_cast<CurlProvider*>(context);

    // RegisterAppStateChangeNotification reports "quiescing" as isSuspended == TRUE.
    if (isSuspended)
    {
        provider->Suspend();
    }
    else
    {
        provider->Resume();
    }
}

#endif // HC_PLATFORM == HC_PLATFORM_GDK

} // httpclient
} // xbox
