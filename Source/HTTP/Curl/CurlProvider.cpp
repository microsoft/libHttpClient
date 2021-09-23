#include "pch.h"
#include "CurlProvider.h"
#include "CurlEasyRequest.h"

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
    // HC_PERFORM_ENV created with custom deleter - HC_PERFORM_ENV needs to be destroyed and cleaned up explicitly.
    http_stl_allocator<HC_PERFORM_ENV> a{};
    std::allocator_traits<http_stl_allocator<HC_PERFORM_ENV>>::destroy(a, performEnv);
    std::allocator_traits<http_stl_allocator<HC_PERFORM_ENV>>::deallocate(a, performEnv, 1);
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

} // http_client
} // xbox

HC_PERFORM_ENV::HC_PERFORM_ENV()
#if HC_WINHTTP_WEBSOCKETS
    : winHttpState{ http_allocate_shared<xbox::httpclient::WinHttpState>() }
#endif
{
}

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
