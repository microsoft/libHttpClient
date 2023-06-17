#include "pch.h"
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>
#include "AndroidHttpProvider.h"
#include "android_http_request.h"
#include "Platform/Android/PlatformComponents_Android.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

AndroidHttpProvider::AndroidHttpProvider(SharedPtr<PlatformComponents_Android> platformComponents) :
    m_platformComponents{ std::move(platformComponents) }
{
}

HRESULT AndroidHttpProvider::PerformAsync(
    HCCallHandle call,
    XAsyncBlock* asyncBlock
) noexcept
{
    std::unique_ptr<HttpRequest> httpRequest{
        new HttpRequest(
            asyncBlock,
            m_platformComponents->GetJavaVm(),
            m_platformComponents->GetApplicationContext(),
            m_platformComponents->GetHttpRequestClass(),
            m_platformComponents->GetHttpResponseClass()
        )
    };

    HRESULT result = httpRequest->Initialize();

    if (!SUCCEEDED(result))
    {
        HCHttpCallResponseSetNetworkErrorCode(call, result, 0);
        XAsyncComplete(asyncBlock, result, 0);
        return S_OK;
    }

    const char* requestUrl = nullptr;
    const char* requestMethod = nullptr;

    HCHttpCallRequestGetUrl(call, &requestMethod, &requestUrl);
    httpRequest->SetUrl(requestUrl);

    uint32_t numHeaders = 0;
    HCHttpCallRequestGetNumHeaders(call, &numHeaders);

    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* headerName = nullptr;
        const char* headerValue = nullptr;

        HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
        httpRequest->AddHeader(headerName, headerValue);
    }

    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t requestBodySize = 0;
    void* readFunctionContext = nullptr;
    HCHttpCallRequestGetRequestBodyReadFunction(call, &readFunction, &requestBodySize, &readFunctionContext);

    const char* contentType = nullptr;
    if (requestBodySize > 0)
    {
        HCHttpCallRequestGetHeader(call, "Content-Type", &contentType);
    }

    httpRequest->SetMethodAndBody(call, requestMethod, contentType, requestBodySize);

    HCHttpCallSetContext(call, httpRequest.get());
    result = httpRequest->ExecuteAsync(call);

    if (SUCCEEDED(result))
    {
        httpRequest.release();
    }
    else
    {
        XAsyncComplete(asyncBlock, E_FAIL, 0);
    }

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END