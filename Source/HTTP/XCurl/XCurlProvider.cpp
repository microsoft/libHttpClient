#include "pch.h"
#include "XCurlProvider.h"
#include "XCurlEasyRequest.h"

using namespace xbox::http_client;

HRESULT Internal_InitializeHttpPlatform(HCInitArgs* args, PerformEnv& performEnv) noexcept
{
    assert(args == nullptr);
    UNREFERENCED_PARAMETER(args);

    auto initResult = HC_PERFORM_ENV::Initialize();
    RETURN_IF_FAILED(initResult.hr);

    performEnv = initResult.ExtractPayload();

    return S_OK;
}

void Internal_CleanupHttpPlatform(HC_PERFORM_ENV* performEnv) noexcept
{
    // HC_PERFORM_ENV created with custom deleter - ~HC_PERFORM_ENV needs to be explicitly invoked.
    performEnv->~HC_PERFORM_ENV();
}

HRESULT Internal_SetGlobalProxy(
    _In_ HC_PERFORM_ENV* performEnv,
    _In_ const char* proxyUri
) noexcept
{
    UNREFERENCED_PARAMETER(performEnv);
    UNREFERENCED_PARAMETER(proxyUri);
    return E_NOTIMPL;
}

void CALLBACK Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept
{
    UNREFERENCED_PARAMETER(context);
    assert(env);

    HRESULT hr = env->Perform(call, asyncBlock);
    if (FAILED(hr))
    {
        // Complete XAsyncBlock if we fail synchronously
        XAsyncComplete(asyncBlock, hr, 0);
    }
}

namespace xbox
{
namespace http_client
{

HRESULT HrFromCurle(CURLcode c) noexcept
{
    if (c == CURLE_OK)
    {
        return S_OK;
    }
    else
    {
        return E_FAIL;
    }
}

HRESULT HrFromCurlm(CURLMcode c) noexcept
{
    if (c == CURLM_OK)
    {
        return S_OK;
    }
    else
    {
        return E_FAIL;
    }
}

} // http_client
} // xbox

Result<PerformEnv> HC_PERFORM_ENV::Initialize()
{
    CURLcode initRes = curl_global_init(CURL_GLOBAL_ALL);
    RETURN_IF_FAILED(HrFromCurle(initRes));

    http_stl_allocator<HC_PERFORM_ENV> a{};
    PerformEnv env{ new (a.allocate(1)) HC_PERFORM_ENV{} };

    return std::move(env);
}

HC_PERFORM_ENV::~HC_PERFORM_ENV()
{
    // make sure XCurlMultis are cleaned up before curl_global_cleanup
    m_curlMultis.clear();

    curl_global_cleanup();
}

HRESULT HC_PERFORM_ENV::Perform(HCCallHandle hcCall, XAsyncBlock* async) noexcept
{
    XTaskQueuePortHandle workPort{ nullptr };
    RETURN_IF_FAILED(XTaskQueueGetPort(async->queue, XTaskQueuePort::Work, &workPort));

    HC_TRACE_VERBOSE(HTTPCLIENT, "HC_PERFORM_ENV::Perform: HCCallHandle=%p, workPort=%p", hcCall, workPort);

    auto iter = m_curlMultis.find(workPort);
    if (iter == m_curlMultis.end())
    {
        auto multiInitResult = XCurlMulti::Initialize(workPort);
        RETURN_IF_FAILED(multiInitResult.hr);

        iter = m_curlMultis.emplace(workPort, multiInitResult.ExtractPayload()).first;
    }

    auto& multi{ iter->second };

    auto easyInitResult = XCurlEasyRequest::Initialize(hcCall, async);
    RETURN_IF_FAILED(easyInitResult.hr);
    RETURN_IF_FAILED(multi->AddRequest(easyInitResult.ExtractPayload()));

    return S_OK;
}
