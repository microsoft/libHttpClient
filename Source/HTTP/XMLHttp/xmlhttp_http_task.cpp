// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if !HC_XDK_API
#include <Shlwapi.h>
#endif
#include "../httpcall.h"
#include "xmlhttp_http_task.h"
#include "http_request_callback.h"
#include "http_response_stream.h"

xmlhttp_http_task::xmlhttp_http_task(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    ) :
    m_statusCode(0),
    m_call(call),
    m_taskHandle(taskHandle)
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
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
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
            HC_RESULT hrTranslated = (SUCCEEDED(hr)) ? HC_OK : HC_E_FAIL;
            HCHttpCallResponseSetNetworkErrorCode(call, hrTranslated, hr);
            HCTaskSetCompleted(taskHandle);
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
            HC_RESULT hrTranslated = (SUCCEEDED(hr)) ? HC_OK : HC_E_FAIL;
            HCHttpCallResponseSetNetworkErrorCode(call, hrTranslated, hr);
            HCTaskSetCompleted(taskHandle);
            return;
        }

        m_hRequest->SetProperty(XHR_PROP_NO_CRED_PROMPT, TRUE);

        ULONGLONG timeout = static_cast<ULONGLONG>(timeoutInSeconds * 1000);
        m_hRequest->SetProperty(XHR_PROP_TIMEOUT, timeout);

#ifdef XHR_PROP_ONDATA_NEVER
        m_hRequest->SetProperty(XHR_PROP_ONDATA_THRESHOLD, XHR_PROP_ONDATA_NEVER);
#endif

        m_hRequest->SetRequestHeader(L"User-Agent", L"libHttpClient/1.0.0.0");
        for (uint32_t i = 0; i < numHeaders; i++)
        {
            const char* iHeaderName;
            const char* iHeaderValue;
            HCHttpCallRequestGetHeaderAtIndex(call, i, &iHeaderName, &iHeaderValue);
            if (iHeaderName != nullptr && iHeaderValue != nullptr)
            {
                hr = m_hRequest->SetRequestHeader(utf16_from_utf8(iHeaderName).c_str(), utf16_from_utf8(iHeaderValue).c_str());
            }
        }

        hr = m_hRequest->SetCustomResponseStream(Microsoft::WRL::Make<http_response_stream>(httpTask).Get());
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to set HTTP response stream %lu", hr);
            HC_RESULT hrTranslated = (SUCCEEDED(hr)) ? HC_OK : HC_E_FAIL;
            HCHttpCallResponseSetNetworkErrorCode(call, hrTranslated, hr);
            HCTaskSetCompleted(taskHandle);
            return;
        }

#if !HC_XDK_API
        if (requestBodyBytes > 0 && requestBody != nullptr)
        {
            m_hRequestBodyStream.Attach(SHCreateMemStream(requestBody, requestBodyBytes));
            if (m_hRequestBodyStream == nullptr)
            {
                HC_TRACE_ERROR(HTTPCLIENT, "[%d] SHCreateMemStream failed in xmlhttp_http_task.",
                    HC_E_OUTOFMEMORY);

                HCHttpCallResponseSetNetworkErrorCode(call, HC_E_OUTOFMEMORY, HC_E_OUTOFMEMORY);
                HCTaskSetCompleted(taskHandle);
                return;
            }
        }
#endif

        hr = m_hRequest->Send(m_hRequestBodyStream.Get(), requestBodyBytes);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to send HTTP request %lu", hr);
            HC_RESULT hrTranslated = (SUCCEEDED(hr)) ? HC_OK : HC_E_FAIL;
            HCHttpCallResponseSetNetworkErrorCode(call, hrTranslated, hr);
            HCTaskSetCompleted(taskHandle);
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
            HC_E_OUTOFMEMORY, e.what());

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_OUTOFMEMORY, static_cast<uint32_t>(HC_E_OUTOFMEMORY));
        HCTaskSetCompleted(taskHandle);
    }
    catch (std::exception const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception in xmlhttp_http_task: %s",
            HC_E_FAIL, e.what());

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_FAIL, static_cast<uint32_t>(HC_E_FAIL));
        HCTaskSetCompleted(taskHandle);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception in xmlhttp_http_task", HC_E_FAIL);

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_FAIL, static_cast<uint32_t>(HC_E_FAIL));
        HCTaskSetCompleted(taskHandle);
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

    HC_ASSERT(m_headerNames.size() == m_headerValues.size());
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

HC_CALL_HANDLE xmlhttp_http_task::call()
{
    return m_call;
}

HC_TASK_HANDLE xmlhttp_http_task::task_handle()
{
    return m_taskHandle;
}

void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
)
{
    std::shared_ptr<xmlhttp_http_task> httpTask = http_allocate_shared<xmlhttp_http_task>(call, taskHandle);
    call->task = httpTask;
    httpTask->perform_async(call, taskHandle);
}

