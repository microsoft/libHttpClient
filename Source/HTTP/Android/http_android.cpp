// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if ANDROID_API
#include <httpClient/httpClient.h>
#include "android_http_request.h"

void Internal_HCHttpCallInitialize(void* context)
{
    HttpRequest::InitializeJavaEnvironment(reinterpret_cast<JavaVM*>(context));
}

void Internal_HCHttpCallPerform(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    )
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "Internal_HCHttpCallPerform");
    HttpRequest httpRequest;
    HRESULT result = httpRequest.Initialize();

    if (!SUCCEEDED(result))
    {
        CompleteAsync(asyncBlock, result, 0);
        return;
    }

    const char* requestUrl = nullptr;
    const char* requestMethod = nullptr;

    HCHttpCallRequestGetUrl(call, &requestMethod, &requestUrl);
    httpRequest.SetUrl(requestUrl);

    uint32_t numHeaders = 0;
    HCHttpCallRequestGetNumHeaders(call, &numHeaders);

    for (uint32_t i = 0; i < numHeaders; i++) 
    {
        const char* headerName = nullptr;
        const char* headerValue = nullptr;

        HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
        httpRequest.AddHeader(headerName, headerValue);
    }

    const uint8_t* requestBody = nullptr;
    uint32_t requestBodySize = 0;

    HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodySize);
    if (requestBodySize > 0) 
    {
        const char* contentType = nullptr;
        HCHttpCallRequestGetHeader(call, "Content-Type", &contentType);
        httpRequest.SetMethodAndBody(requestMethod, contentType, requestBody, requestBodySize);
    }

    result = httpRequest.ExecuteRequest();

    if (!SUCCEEDED(result)) 
    { 
        CompleteAsync(asyncBlock, result, 0);
        return;
    }

    HCHttpCallResponseSetStatusCode(call, httpRequest.GetResponseCode());

    for (uint32_t i = 0; i < httpRequest.GetResponseHeaderCount(); i++) 
    {
        std::string headerName = httpRequest.GetHeaderNameAtIndex(i);
        std::string headerValue = httpRequest.GetHeaderValueAtIndex(i);
        HCHttpCallResponseSetHeader(call, headerName.c_str(), headerValue.c_str());
    }

    httpRequest.ProcessResponseBody(call);

    CompleteAsync(asyncBlock, S_OK, 0);
}

#endif
