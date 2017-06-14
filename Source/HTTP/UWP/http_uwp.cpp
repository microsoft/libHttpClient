// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if UWP_API
#include "httpClient/types.h"
#include "httpClient/httpClient.h"
#include "singleton.h"
#include "asyncop.h"
#include "UWP/utils_uwp.h"

using namespace Windows::Foundation;
using namespace Windows::Web::Http;

void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call
    )
{
    const WCHAR* url = nullptr;
    const WCHAR* method = nullptr;
    const WCHAR* requestBody = nullptr;
    const WCHAR* userAgent = nullptr;
    HCHttpCallRequestGetUrl(call, &method, &url);
    HCHttpCallRequestGetRequestBodyString(call, &requestBody);
    HCHttpCallRequestGetHeader(call, L"User-Agent", &userAgent);

    try
    {
        HttpClient^ httpClient = ref new HttpClient();
        auto headers = httpClient->DefaultRequestHeaders;
        Uri^ requestUri = ref new Uri(ref new Platform::String(url));

        auto taskHttpClientGet = HttpTaskWithProgress<Windows::Web::Http::HttpResponseMessage^, Windows::Web::Http::HttpProgress>::New();
        taskHttpClientGet->Init(httpClient->GetAsync(requestUri));
        taskHttpClientGet->WaitForever();
        HttpResponseMessage^ httpResponse = taskHttpClientGet->GetResult();

        auto taskReadAsString = HttpTaskWithProgress<Platform::String^, unsigned long long>::New();
        taskReadAsString->Init(httpResponse->Content->ReadAsStringAsync());
        taskReadAsString->WaitForever();
        Platform::String^ httpResponseBody = taskReadAsString->GetResult();

        HCHttpCallResponseSetResponseString(call, httpResponseBody->Data());
    }
    catch (Platform::Exception^ ex)
    {
        HCHttpCallResponseSetErrorCode(call, ex->HResult);
    }
}

#endif