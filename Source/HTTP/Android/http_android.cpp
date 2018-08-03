// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if ANDROID_API
#include <httpClient/httpClient.h>
#include "android_http_request.h"
#include "android_platform_context.h"

extern "C"
{

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted(JNIEnv* env, jobject instance, jlong call, jobject response)
{
    hc_call_handle_t sourceCall = reinterpret_cast<hc_call_handle_t>(call);
    HttpRequest* request = nullptr;
    HCHttpCallGetContext(sourceCall, reinterpret_cast<void**>(&request));
    std::unique_ptr<HttpRequest> sourceRequest{ request };

    if (response == nullptr) 
    {
        CompleteAsync(sourceRequest->GetAsyncBlock(), E_FAIL, 0);
    }
    else 
    {
        HRESULT result = sourceRequest->ProcessResponse(sourceCall, response);
        CompleteAsync(sourceRequest->GetAsyncBlock(), result, 0);
    }
}

}

void Internal_HCHttpCallPerformAsync(
    _In_ hc_call_handle_t call,
    _Inout_ AsyncBlock* asyncBlock
    )
{
    auto httpSingleton = xbox::httpclient::get_http_singleton(true);
    AndroidPlatformContext* platformContext = reinterpret_cast<AndroidPlatformContext*>(httpSingleton->m_platformContext.get());
    std::unique_ptr<HttpRequest> httpRequest{
        new HttpRequest(
            asyncBlock,
            platformContext->GetJavaVm(),
            platformContext->GetApplicationContext(),
            platformContext->GetHttpRequestClass(),
            platformContext->GetHttpResponseClass()
        )
    };

    HRESULT result = httpRequest->Initialize();

    if (!SUCCEEDED(result))
    {
        CompleteAsync(asyncBlock, result, 0);
        return;
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

    const uint8_t* requestBody = nullptr;
    const char* contentType = nullptr;
    uint32_t requestBodySize = 0;

    HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodySize);

    if (requestBodySize > 0)
    {
        HCHttpCallRequestGetHeader(call, "Content-Type", &contentType);
    }

    httpRequest->SetMethodAndBody(requestMethod, contentType, requestBody, requestBodySize);

    HCHttpCallSetContext(call, httpRequest.get());
    result = httpRequest->ExecuteAsync(call);

    if (SUCCEEDED(result))
    {
        httpRequest.release();
    }
    else
    { 
        CompleteAsync(asyncBlock, E_FAIL, 0);
    }
}

#endif
