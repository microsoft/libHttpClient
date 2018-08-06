// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if !HC_XDK_API
#include <Shlwapi.h>
#endif
#include "../httpcall.h"
#include "xmlhttp_http_task.h"
#include "utils.h"
#include "http_request_callback.h"
#include "http_response_stream.h"
#include "http_request_stream.h"

xmlhttp_http_task::xmlhttp_http_task(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    ) :
    m_statusCode(0),
    m_call(call),
    m_asyncBlock(asyncBlock)
{
    m_hrCoInit = CoInitializeEx(nullptr, 0);
}

xmlhttp_http_task::~xmlhttp_http_task()
{
    m_hRequest = nullptr;
    m_hRequestBodyStream = nullptr;
    if (SUCCEEDED(m_hrCoInit))
    {
        CoUninitialize();
    }
}

void xmlhttp_http_task::perform_async(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    )
{
    try
    {
        const char* url = nullptr;
        const char* method = nullptr;
        const BYTE* requestBody = nullptr;
        uint32_t requestBodyBytes = 0;
        HCHttpCallRequestGetUrl(call, &method, &url);
        HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodyBytes);

        uint32_t numHeaders = 0;
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);

        uint32_t timeoutInSeconds = 0;
        HCHttpCallRequestGetTimeout(call, &timeoutInSeconds);

        HRESULT hr = CoCreateInstance(
            __uuidof(FreeThreadedXMLHTTP60),
            nullptr,
#if HC_XDK_API
            CLSCTX_SERVER,
#else
            CLSCTX_INPROC,
#endif
            __uuidof(IXMLHTTPRequest2),
            reinterpret_cast<void**>(m_hRequest.GetAddressOf()));
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to create IXMLHTTPRequest2 instance %lu", hr);
            HCHttpCallResponseSetNetworkErrorCode(call, hr, hr);
            CompleteAsync(asyncBlock, hr, 0);
            return;
        }

        std::shared_ptr<hc_task> httpTask2 = shared_from_this();
        std::shared_ptr<xmlhttp_http_task> httpTask = std::dynamic_pointer_cast<xmlhttp_http_task>(httpTask2);

        http_internal_wstring wMethod = utf16_from_utf8(method);
        http_internal_wstring wUrl = utf16_from_utf8(url);
        hr = m_hRequest->Open(
            wMethod.c_str(),
            wUrl.c_str(),
            Microsoft::WRL::Make<http_request_callback>(httpTask).Get(),
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to open HTTP request %lu", hr);
            HCHttpCallResponseSetNetworkErrorCode(call, hr, hr);
            CompleteAsync(asyncBlock, hr, 0);
            return;
        }

        m_hRequest->SetProperty(XHR_PROP_NO_CRED_PROMPT, TRUE);

        ULONGLONG timeout = static_cast<ULONGLONG>(timeoutInSeconds * 1000);
        m_hRequest->SetProperty(XHR_PROP_TIMEOUT, timeout);

#ifdef XHR_PROP_ONDATA_NEVER
        m_hRequest->SetProperty(XHR_PROP_ONDATA_THRESHOLD, XHR_PROP_ONDATA_NEVER);
#endif

        bool userAgentSet = false;
        for (uint32_t i = 0; i < numHeaders; i++)
        {
            const char* iHeaderName;
            const char* iHeaderValue;
            HCHttpCallRequestGetHeaderAtIndex(call, i, &iHeaderName, &iHeaderValue);
            if (iHeaderName != nullptr && iHeaderValue != nullptr)
            {
                if (xbox::httpclient::str_icmp(iHeaderName, "User-Agent") == 0)
                {
                    userAgentSet = true;
                }

                hr = m_hRequest->SetRequestHeader(utf16_from_utf8(iHeaderName).c_str(), utf16_from_utf8(iHeaderValue).c_str());
            }
        }

        if (!userAgentSet)
        {
            m_hRequest->SetRequestHeader(L"User-Agent", L"libHttpClient/1.0.0.0");
        }

        hr = m_hRequest->SetCustomResponseStream(Microsoft::WRL::Make<http_response_stream>(httpTask).Get());
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to set HTTP response stream %lu", hr);
            HCHttpCallResponseSetNetworkErrorCode(call, hr, hr);
            CompleteAsync(asyncBlock, hr, 0);
            return;
        }

        if (requestBodyBytes > 0 && requestBody != nullptr)
        {
            auto requestStream = Microsoft::WRL::Make<http_request_stream>();
            if (requestStream != nullptr)
            {
                hr = requestStream->init(requestBody, requestBodyBytes);
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }

            if (FAILED(hr))
            {
                HC_TRACE_ERROR(HTTPCLIENT, "[%d] http_request_stream failed in xmlhttp_http_task.", hr);
                HCHttpCallResponseSetNetworkErrorCode(call, E_FAIL, static_cast<uint32_t>(hr));
                CompleteAsync(asyncBlock, hr, 0);
                return;
            }

            hr = m_hRequest->Send(requestStream.Get(), requestBodyBytes);
        }
        else
        {
            hr = m_hRequest->Send(nullptr, 0);
        }

        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to send HTTP request %lu", hr);
            HCHttpCallResponseSetNetworkErrorCode(call, hr, hr);
            CompleteAsync(asyncBlock, hr, 0);
            return;
        }
        // If there were no errors so far, HCTaskSetCompleted is called later 
        // http_request_callback::OnResponseReceived 
        // or 
        // http_request_callback::OnError
    }
    catch (std::bad_alloc const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::bad_alloc in xmlhttp_http_task: %s",
            E_OUTOFMEMORY, e.what());

        HCHttpCallResponseSetNetworkErrorCode(call, E_OUTOFMEMORY, static_cast<uint32_t>(E_OUTOFMEMORY));
        CompleteAsync(asyncBlock, E_OUTOFMEMORY, 0);
    }
    catch (std::exception const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception in xmlhttp_http_task: %s",
            E_FAIL, e.what());

        HCHttpCallResponseSetNetworkErrorCode(call, E_FAIL, static_cast<uint32_t>(E_FAIL));
        CompleteAsync(asyncBlock, E_FAIL, 0);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception in xmlhttp_http_task", E_FAIL);

        HCHttpCallResponseSetNetworkErrorCode(call, E_FAIL, static_cast<uint32_t>(E_FAIL));
        CompleteAsync(asyncBlock, E_FAIL, 0);
    }
}

void xmlhttp_http_task::set_status_code(_In_ uint32_t statusCode)
{
    m_statusCode = statusCode;
}

uint32_t xmlhttp_http_task::get_status_code()
{
    return m_statusCode;
}

http_internal_vector<http_internal_string> xmlhttp_http_task::split(
    _In_ const http_internal_string& s, 
    _In_z_ const char* delim)
{
    http_internal_vector<http_internal_string> v;
    size_t i = 0;
    size_t delimLen = strlen(delim);
    auto pos = s.find(delim);
    while (pos != std::string::npos) 
    {
        v.push_back(s.substr(i, pos - i));
        i = pos + delimLen;
        pos = s.find(delim, i);
    }
    v.push_back(s.substr(i, s.length()));

    return v;
}

void xmlhttp_http_task::set_headers(_In_ WCHAR* allResponseHeaders)
{
    auto allHeaders = utf8_from_utf16(allResponseHeaders);
    auto splitHeaders = split(allHeaders, "\r\n");
    for (auto& header : splitHeaders)
    {
        auto colonPos = header.find(':');
        if (colonPos == std::string::npos || colonPos == 0)
        {
            // Invalid header found
            continue;
        }

        m_headerNames.push_back(header.substr(0, colonPos));
        size_t valueStartPos = colonPos + 1; // skip the colon
        valueStartPos = header.find_first_not_of(" \t", valueStartPos); // skip all leading optional whitespace
        if (valueStartPos != std::string::npos)
        {
            size_t valueEndPos = header.find_last_not_of(" \t");
            m_headerValues.push_back(header.substr(valueStartPos, valueEndPos - valueStartPos + 1));
        }
        else
        {
            m_headerValues.push_back("");
        }
    }

    ASSERT(m_headerNames.size() == m_headerValues.size());
}

const http_internal_vector<http_internal_string>& xmlhttp_http_task::get_headers_names()
{
    return m_headerNames;
}

const http_internal_vector<http_internal_string>& xmlhttp_http_task::get_headers_values()
{
    return m_headerValues;
}

bool xmlhttp_http_task::has_error()
{
    return m_exceptionPtr != nullptr;
}

void xmlhttp_http_task::set_exception(const std::exception_ptr& exceptionPtr)
{
    m_exceptionPtr = exceptionPtr;
}

http_buffer& xmlhttp_http_task::response_buffer()
{
    return m_responseBuffer;
}

hc_call_handle_t xmlhttp_http_task::call()
{
    return m_call;
}

AsyncBlock* xmlhttp_http_task::async_block()
{
    return m_asyncBlock;
}


HRESULT IHCPlatformContext::InitializeHttpPlatformContext(HCInitArgs* args, IHCPlatformContext** platformContext)
{
    // No-op
    assert(args == nullptr);
    *platformContext = nullptr;
    return S_OK;
}

void Internal_HCHttpCallPerformAsync(
    _In_ hc_call_handle_t call,
    _Inout_ AsyncBlock* asyncBlock
    )
{
    auto httpTask = http_allocate_shared<xmlhttp_http_task>(asyncBlock, call);
    httpTask->perform_async(asyncBlock, call);
}

