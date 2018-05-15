// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if ANDROID_API
#include <httpClient/httpClient.h>
#include "android_http_request.h"

STDAPI
HCInitializeJavaEnvironment(void* context) HC_NOEXCEPT
try {
    return HttpRequest::InitializeJavaEnvironment(reinterpret_cast<JavaVM*>(context));
}
CATCH_RETURN();

void Internal_HCHttpCallPerform(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    )
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "Initializing HTTP request");

    // TODO
    HttpRequest httpRequest;

    const char* requestUrl = nullptr;
    const char* requestMethod = nullptr;

    HCHttpCallRequestGetUrl(call, &requestMethod, &requestUrl);
    httpRequest.SetUrl(requestUrl);

    uint32_t numHeaders = 0;
    HCHttpCallRequestGetNumHeaders(call, &numHeaders);
    HC_TRACE_INFORMATION(HTTPCLIENT, "Found %d headers", numHeaders);

    for (uint32_t i = 0; i < numHeaders; i++) {
        const char* headerName = nullptr;
        const char* headerValue = nullptr;

        // TODO: Check the HRESULT?
        HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
        HC_TRACE_INFORMATION(HTTPCLIENT, "Header %d: %s: %s", i, headerName, headerValue);
        httpRequest.AddHeader(headerName, headerValue);
    }

    const uint8_t* requestBody = nullptr;
    uint32_t requestBodySize = 0;

    // TODO: Set request body
    HC_TRACE_INFORMATION(HTTPCLIENT, "Executing HTTP request");
    httpRequest.ExecuteRequest();

    HCHttpCallResponseSetStatusCode(call, httpRequest.GetResponseCode());

    for (uint32_t i = 0; i < httpRequest.GetResponseHeaderCount(); i++) {
        std::string headerName = httpRequest.GetHeaderNameAtIndex(i);
        std::string headerValue = httpRequest.GetHeaderValueAtIndex(i);
        HCHttpCallResponseSetHeader(call, headerName.c_str(), headerValue.c_str());
    }

    httpRequest.ProcessResponseBody(call);

    CompleteAsync(asyncBlock, S_OK, 0);
}

#endif
