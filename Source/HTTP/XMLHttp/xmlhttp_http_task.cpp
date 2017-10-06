// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
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
        http_internal_string headerName;
        http_internal_string headerValue;
        const char* url = nullptr;
        const char* method = nullptr;
        const char* requestBody = nullptr;
        const char* userAgent = nullptr;
        HCHttpCallRequestGetUrl(call, &method, &url);
        HCHttpCallRequestGetRequestBodyString(call, &requestBody);

        uint32_t numHeaders = 0;
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);

        uint32_t timeoutInSeconds = 0;
        HCHttpCallRequestGetTimeout(call, &timeoutInSeconds);

        HRESULT hr = CoCreateInstance(
            __uuidof(FreeThreadedXMLHTTP60),
            nullptr,
#if XDK_API
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
            const char* headerName;
            const char* headerValue;
            HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
            if (headerName != nullptr && headerValue != nullptr)
            {
                hr = m_hRequest->SetRequestHeader(utf16_from_utf8(headerName).c_str(), utf16_from_utf8(headerValue).c_str());
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

        hr = m_hRequest->Send(nullptr, 0);
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

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_OUTOFMEMORY, HC_E_OUTOFMEMORY);
        HCTaskSetCompleted(taskHandle);
    }
    catch (std::exception const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception in xmlhttp_http_task: %s",
            HC_E_FAIL, e.what());

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_FAIL, HC_E_FAIL);
        HCTaskSetCompleted(taskHandle);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception in xmlhttp_http_task", HC_E_FAIL);

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_FAIL, HC_E_FAIL);
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
    _In_ char delim)
{
    http_internal_vector<http_internal_string> v;
    size_t i = 0;
    auto pos = s.find(delim);
    while (pos != std::string::npos) 
    {
        v.push_back(s.substr(i, pos - i));
        i = ++pos;
        pos = s.find(delim, pos);
    }
    v.push_back(s.substr(i, s.length()));

    return v;
}

void xmlhttp_http_task::set_headers(_In_ WCHAR* allResponseHeaders)
{
    auto allHeaders = utf8_from_utf16(allResponseHeaders);
    auto& splitHeaders = split(allHeaders, '\n');
    for (auto& header : splitHeaders)
    {
        auto& headerNameValue = split(header, ':');
        if (headerNameValue.size() == 2)
        {
            m_headerNames.push_back(headerNameValue[0]);

            if (headerNameValue[1].length() > 0 &&
                headerNameValue[1][0] == ' ')
            {
                headerNameValue[1].erase(0, 1); // trim the space
            }

            auto index = headerNameValue[1].find_last_of('\r');
            if (index != std::string::npos)
            {
                headerNameValue[1].erase(index, index + 1);
            }
            m_headerValues.push_back(headerNameValue[1]);
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
    call->task = std::dynamic_pointer_cast<hc_task>(httpTask);

    httpTask->perform_async(call, taskHandle);
}

