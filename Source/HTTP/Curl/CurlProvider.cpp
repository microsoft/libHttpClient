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

Result<std::shared_ptr<CurlProvider>> CurlProvider::Initialize()
{
    CURLcode initRes = curl_global_init(CURL_GLOBAL_ALL);
    RETURN_IF_FAILED(HrFromCurle(initRes));

    http_stl_allocator<CurlProvider> a{};
    auto provider = std::shared_ptr<CurlProvider>{ new (a.allocate(1)) CurlProvider, http_alloc_deleter<CurlProvider>() };

    return std::move(provider);
}

CurlProvider::~CurlProvider()
{
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

} // httpclient
} // xbox
