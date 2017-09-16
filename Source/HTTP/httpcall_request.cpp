// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR method,
    _In_ PCSTR url
    )
{
    if (call == nullptr || method == nullptr || url == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

        call->method = method;
        call->url = url;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetUrl [ID %llu]: method=%s url=%s",
            call->id, method, url);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetUrl(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR* method,
    _Outptr_ PCSTR* url
    )
{
    if (call == nullptr || method == nullptr || url == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

        *method = call->method.c_str();
        *url = call->url.c_str();
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR requestBodyString
    )
{
    if (call == nullptr || requestBodyString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();
        call->requestBodyString = requestBodyString;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetBodyString [ID %llu]: requestBodyString=%s",
            call->id, requestBodyString);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR* requestBodyString
    )
{
    if (call == nullptr || requestBodyString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        *requestBodyString = call->requestBodyString.c_str();
    );
}


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _In_ PCSTR headerValue
    )
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        call->requestHeaders[headerName] = headerValue;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetHeader [ID %llu]: %s=%s",
            call->id, headerName, headerValue);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    )
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto it = call->requestHeaders.find(headerName);
        if (it != call->requestHeaders.end())
        {
            *headerValue = it->second.c_str();
        }
        else
        {
            *headerValue = nullptr;
        }
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    )
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        *numHeaders = static_cast<uint32_t>(call->requestHeaders.size());
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    )
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        uint32_t index = 0;
        for (auto it = call->requestHeaders.cbegin(); it != call->requestHeaders.cend(); ++it)
        {
            if (index == headerIndex)
            {
                *headerName = it->first.c_str();
                *headerValue = it->second.c_str();
                return HC_OK;
            }

            index++;
        }

        *headerName = nullptr;
        *headerValue = nullptr;
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ bool retryAllowed
    )
{
    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            httpSingleton->m_retryAllowed = retryAllowed;
        }
        else
        {
            call->retryAllowed = retryAllowed;

            HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryAllowed [ID %llu]: retryAllowed=%s",
                call->id, retryAllowed ? "true" : "false");
        }
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ bool* retryAllowed
    )
{
    if (retryAllowed == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            *retryAllowed = httpSingleton->m_retryAllowed;
        }
        else
        {
            *retryAllowed = call->retryAllowed;
        }
    );
}

// TODO: verify only can happen prior to perform for this and other APIs
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    )
{
    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            httpSingleton->m_timeoutInSeconds = timeoutInSeconds;
        }
        else
        {
            call->timeoutInSeconds = timeoutInSeconds;
        }

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetTimeout [ID %llu]: timeoutInSeconds=%u",
            call->id, timeoutInSeconds);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutInSeconds
    )
{
    if (timeoutInSeconds == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            *timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
        }
        else
        {
            *timeoutInSeconds = call->timeoutInSeconds;
        }
    );
}


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutWindowInSeconds
    )
{
    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            httpSingleton->m_timeoutWindowInSeconds = timeoutWindowInSeconds;
        }
        else
        {
            call->timeoutWindowInSeconds = timeoutWindowInSeconds;
        }

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestTimeoutWindow: %u", timeoutWindowInSeconds);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t* retryDelayInSeconds
    )
{
    if (retryDelayInSeconds == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            *retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
        }
        else
        {
            *retryDelayInSeconds = call->retryDelayInSeconds;
        }
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t retryDelayInSeconds
    )
{
    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            httpSingleton->m_retryDelayInSeconds = retryDelayInSeconds;
        }
        else
        {
            call->retryDelayInSeconds = retryDelayInSeconds;
        }
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutWindowInSeconds
    )
{
    if (timeoutWindowInSeconds == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            *timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
        }
        else
        {
            *timeoutWindowInSeconds = call->timeoutWindowInSeconds;
        }
    );
}

/// TODO: The asserts will not fire in RETAIL sandbox, and this setting has has no affect in RETAIL sandboxes.
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetAssertsForThrottling(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ bool enableAssertsForThrottling
    )
{
    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            httpSingleton->m_enableAssertsForThrottling = enableAssertsForThrottling;
        }
        else
        {
            call->enableAssertsForThrottling = enableAssertsForThrottling;
        }
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetAssertsForThrottling(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ bool* enableAssertsForThrottling
    )
{
    if (enableAssertsForThrottling == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        if (call == nullptr)
        {
            auto httpSingleton = get_http_singleton();
            *enableAssertsForThrottling = httpSingleton->m_enableAssertsForThrottling;
        }
        else
        {
            *enableAssertsForThrottling = call->enableAssertsForThrottling;
        }
    );
}

