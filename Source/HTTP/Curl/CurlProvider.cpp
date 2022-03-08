#include "pch.h"
#include "CurlProvider.h"
#include "CurlEasyRequest.h"

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
    case CURLMcode::CURLM_BAD_FUNCTION_ARGUMENT: assert(false); return E_INVALIDARG;
    default: return E_FAIL;
    }
}

Result<HC_UNIQUE_PTR<CurlProvider>> CurlProvider::Initialize()
{
    CURLcode initRes = curl_global_init(CURL_GLOBAL_ALL);
    RETURN_IF_FAILED(HrFromCurle(initRes));

    http_stl_allocator<CurlProvider> a{};
    auto provider = HC_UNIQUE_PTR<CurlProvider>{ new (a.allocate(1)) CurlProvider };

    return std::move(provider);
}

CurlProvider::~CurlProvider()
{
    // Either CleanupAsync was never called or CurlProvider shouldn't be destroyed until it completes.
    assert(!m_cleanupTasksRemaining);

    if (m_multiCleanupQueue)
    {
        XTaskQueueCloseHandle(m_multiCleanupQueue);
    }

    // make sure XCurlMultis are cleaned up before curl_global_cleanup
    m_curlMultis.clear();

    curl_global_cleanup();
}

void CALLBACK CurlProvider::PerformAsyncHandler(
    HCCallHandle callHandle,
    XAsyncBlock* async,
    void* /*context*/,
    HCPerformEnv env
) noexcept
{
    assert(env);

    HRESULT hr = env->curlProvider->PerformAsync(callHandle, async);
    if (FAILED(hr))
    {
        // Complete XAsyncBlock if we fail synchronously
        XAsyncComplete(async, hr, 0);
    }
}

HRESULT CurlProvider::PerformAsync(HCCallHandle hcCall, XAsyncBlock* async) noexcept
{
    XTaskQueuePortHandle workPort{ nullptr };
    RETURN_IF_FAILED(XTaskQueueGetPort(async->queue, XTaskQueuePort::Work, &workPort));

    HC_TRACE_VERBOSE(HTTPCLIENT, "HC_PERFORM_ENV::Perform: HCCallHandle=%p, workPort=%p", hcCall, workPort);

    auto easyInitResult = CurlEasyRequest::Initialize(hcCall, async);
    RETURN_IF_FAILED(easyInitResult.hr);

    auto iter = m_curlMultis.find(workPort);
    if (iter == m_curlMultis.end())
    {
        auto multiInitResult = CurlMulti::Initialize(workPort);
        RETURN_IF_FAILED(multiInitResult.hr);

        iter = m_curlMultis.emplace(workPort, multiInitResult.ExtractPayload()).first;
    }

    auto& multi{ iter->second };
    RETURN_IF_FAILED(multi->AddRequest(easyInitResult.ExtractPayload()));

    return S_OK;
}

HRESULT CurlProvider::CleanupAsync(HC_UNIQUE_PTR<CurlProvider> provider, XAsyncBlock* async) noexcept
{
    // CleanupAsync should never be called more than once
    assert(provider->m_cleanupAsyncBlock == nullptr);
    provider->m_cleanupAsyncBlock = async;

    XTaskQueuePortHandle workPort{ nullptr };
    RETURN_IF_FAILED(XTaskQueueGetPort(async->queue, XTaskQueuePort::Work, &workPort));
    RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &provider->m_multiCleanupQueue));

    RETURN_IF_FAILED(XAsyncBegin(async, provider.get(), __FUNCTION__, __FUNCTION__, CleanupAsyncProvider));
    provider.release();

    return S_OK;
}

HRESULT CALLBACK CurlProvider::CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data) noexcept
{
    switch (op)
    {
    case XAsyncOp::Begin:
    {
        HC_UNIQUE_PTR<CurlProvider> provider{ static_cast<CurlProvider*>(data->context) };

        XAsyncBlock multiCleanupAsyncBlock{ provider->m_multiCleanupQueue, provider.get(), CurlProvider::MultiCleanupComplete, 0 };
        provider->m_multiCleanupAsyncBlocks = http_internal_vector<XAsyncBlock>(provider->m_curlMultis.size(), multiCleanupAsyncBlock);

        {
            std::lock_guard<std::mutex> lock{ provider->m_mutex };
            // There is a race condition where the last CurlMulti::CleanupAsync task can complete before the cleanup loop is finished.
            // Because the loop condition relies on the provider being alive, we add an additional cleanup task, ensuring the provider
            // can never be destroyed until after the loop.
            provider->m_cleanupTasksRemaining = 1 + provider->m_curlMultis.size();
        }

        size_t multiIndex{ 0 };
        bool cleanupComplete{ false };

        for (auto& pair : provider->m_curlMultis)
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
            if (--provider->m_cleanupTasksRemaining == 0)
            {
                // If there are no pending pending multi cleanups, complete cleanup here
                cleanupComplete = true;
            }
        }

        if (cleanupComplete)
        {
            provider.reset();
            XAsyncComplete(data->async, S_OK, 0);
        }
        else
        {
            // Release ownership of provider, it will be cleaned up in MultiCleanupComplete
            provider.release();
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
    if (--provider->m_cleanupTasksRemaining == 0)
    {
        // All CurlMultis have finished asyncCleanup. Destroy provider and complete provider's Cleanup XAsyncBlock
        XAsyncBlock* providerCleanupAsyncBlock{ provider->m_cleanupAsyncBlock };

        // Release lock before destroying
        lock.unlock();

        HC_UNIQUE_PTR<CurlProvider> reclaim{ provider };
        reclaim.reset();

        XAsyncComplete(providerCleanupAsyncBlock, S_OK, 0);
    }
}

} // httpclient
} // xbox
