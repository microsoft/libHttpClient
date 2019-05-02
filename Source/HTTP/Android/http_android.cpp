// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>
#include "android_http_request.h"
#include "android_platform_context.h"

extern "C"
{

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted(JNIEnv* env, jobject instance, jlong call, jobject response)
{
    HCCallHandle sourceCall = reinterpret_cast<HCCallHandle>(call);
    HttpRequest* request = nullptr;
    HCHttpCallGetContext(sourceCall, reinterpret_cast<void**>(&request));
    std::unique_ptr<HttpRequest> sourceRequest{ request };

    if (response == nullptr) 
    {
        HCHttpCallResponseSetNetworkErrorCode(sourceCall, E_FAIL, 0);
        XAsyncComplete(sourceRequest->GetAsyncBlock(), E_FAIL, 0);
    }
    else 
    {
        HRESULT result = sourceRequest->ProcessResponse(sourceCall, response);
        XAsyncComplete(sourceRequest->GetAsyncBlock(), result, 0);
    }
}

JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed(JNIEnv* env, jobject instance, jlong call, jstring errorMessage)
{
    HCCallHandle sourceCall = reinterpret_cast<HCCallHandle>(call);
    HttpRequest* request = nullptr;
    HCHttpCallGetContext(sourceCall, reinterpret_cast<void**>(&request));
    std::unique_ptr<HttpRequest> sourceRequest{ request };

    HCHttpCallResponseSetNetworkErrorCode(sourceCall, E_FAIL, 0);

    const char* nativeErrorString = env->GetStringUTFChars(errorMessage, nullptr);
    HCHttpCallResponseSetPlatformNetworkErrorMessage(sourceCall, nativeErrorString);
    env->ReleaseStringUTFChars(errorMessage, nativeErrorString);

    XAsyncComplete(sourceRequest->GetAsyncBlock(), E_FAIL, 0);
}

}

void Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept
{
    auto httpSingleton = xbox::httpclient::get_http_singleton(true);
    std::unique_ptr<HttpRequest> httpRequest{
        new HttpRequest(
            asyncBlock,
            env->GetJavaVm(),
            env->GetApplicationContext(),
            env->GetHttpRequestClass(),
            env->GetHttpResponseClass()
        )
    };

    HRESULT result = httpRequest->Initialize();

    if (!SUCCEEDED(result))
    {
        HCHttpCallResponseSetNetworkErrorCode(call, result, 0);
        XAsyncComplete(asyncBlock, result, 0);
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
        XAsyncComplete(asyncBlock, E_FAIL, 0);
    }
}
