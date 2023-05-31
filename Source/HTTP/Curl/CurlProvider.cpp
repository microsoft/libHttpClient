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

HRESULT CurlProvider::PerformAsync(HCCallHandle hcCall, XAsyncBlock* async) noexcept
{
    XTaskQueuePortHandle workPort{ nullptr };
    RETURN_IF_FAILED(XTaskQueueGetPort(async->queue, XTaskQueuePort::Work, &workPort));

    HC_TRACE_INFORMATION(HTTPCLIENT, "HC_PERFORM_ENV::Perform: HCCallHandle=%p, workPort=%p", hcCall, workPort);

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
    }

    auto& multi{ iter->second };
    RETURN_IF_FAILED(multi->AddRequest(easyInitResult.ExtractPayload()));

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

        XAsyncBlock multiCleanupAsyncBlock{ provider->m_multiCleanupQueue, provider, CurlProvider::MultiCleanupComplete, 0 };
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
    if (--provider->m_cleanupTasksRemaining == 0)
    {
        // All CurlMultis have finished asyncCleanup. Destroy provider and complete provider's Cleanup XAsyncBlock
        XAsyncBlock* providerCleanupAsyncBlock{ provider->m_cleanupAsyncBlock };

        // Release lock before completing async operation, since CurlProvider could be destroyed anytime after XAsyncComplete is called
        lock.unlock();

        XAsyncComplete(providerCleanupAsyncBlock, S_OK, 0);
    }
}

} // httpclient
} // xbox
