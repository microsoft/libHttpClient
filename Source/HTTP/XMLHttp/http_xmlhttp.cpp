// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "../httpcall.h"
#include "win/utils_win.h"
#include "singleton.h"
#include <memory.h>

#include <Strsafe.h>
#if !defined(__WRL_NO_DEFAULT_LIB__)
#define __WRL_NO_DEFAULT_LIB__
#endif
#include <wrl.h>
#if !XDK_API
#include <msxml6.h>
#else
#include <ixmlhttprequest2.h>
#endif

using namespace Microsoft::WRL;
using namespace xbox::httpclient;

class http_buffer
{
public:
    http_buffer()
    {
    }

    errno_t append(
        _In_reads_bytes_(cb) const void* pv,
        _In_ ULONG cb
        )
    {
        const byte* bv = reinterpret_cast<const byte*>(pv);
        std::vector<byte> srcData(bv, bv+cb);
        m_buffer.insert(m_buffer.end(), srcData.begin(), srcData.end());
        return 0;
    }

private:
    http_internal_vector<BYTE> m_buffer;
};

class xmlhttp_http_task : public hc_task
{
private:

public:
    xmlhttp_http_task() :
        m_statusCode(0)
    {
        m_hrCoInit = CoInitializeEx(nullptr, 0);
    }

    ~xmlhttp_http_task()
    {
        if (SUCCEEDED(m_hrCoInit))
        {
            CoUninitialize();
        }
    }

    void perform_async(
        _In_ HC_CALL_HANDLE call,
        _In_ HC_TASK_HANDLE taskHandle
        );

    void set_status_code(_In_ DWORD statusCode)
    {
        m_statusCode = statusCode;
    }

    void set_error_message(_In_ const WCHAR* phrase)
    {
        m_errorMessage = phrase;
    }

    void set_headers(_In_ WCHAR* allResponseHeaders)
    {
        // TODO: parse headers
        m_allResponseHeaders = allResponseHeaders;
    }

    std::exception_ptr m_exceptionPtr;
    http_buffer m_responseBuffer;
    std::wstring m_allResponseHeaders;

private:
    DWORD m_statusCode;
    std::wstring m_errorMessage;
    HRESULT m_hrCoInit;
    Microsoft::WRL::ComPtr<IXMLHTTPRequest2> m_hRequest;
};


void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    std::shared_ptr<xmlhttp_http_task> httpTask = std::make_shared<xmlhttp_http_task>();
    call->task = std::dynamic_pointer_cast<hc_task>(httpTask);

    httpTask->perform_async(call, taskHandle);
}


class HttpRequestCallback : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IXMLHTTPRequest2Callback, Microsoft::WRL::FtmBase>
{
public:
    HttpRequestCallback(_In_ const std::shared_ptr<xmlhttp_http_task>& httpTask) : m_httpTask(httpTask)
    {
    }

    HRESULT STDMETHODCALLTYPE OnRedirect(_In_opt_ IXMLHTTPRequest2*, __RPC__in_string const WCHAR*) 
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnHeadersAvailable(_In_ IXMLHTTPRequest2* xmlReq, DWORD statusCode, __RPC__in_string const WCHAR* phrase)
    {
        m_httpTask->set_status_code(statusCode);
        m_httpTask->set_error_message(phrase);

        WCHAR* allResponseHeaders = nullptr;
        HRESULT hr = xmlReq->GetAllResponseHeaders(&allResponseHeaders);
        if (SUCCEEDED(hr))
        {
            try
            {
                if (allResponseHeaders != nullptr)
                {
                    m_httpTask->set_headers(allResponseHeaders);
                }
            }
            catch (...)
            {
                m_httpTask->m_exceptionPtr = std::current_exception();
                hr = ERROR_UNHANDLED_EXCEPTION;
            }
        }

        if (allResponseHeaders != nullptr)
        {
            ::CoTaskMemFree(allResponseHeaders);
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE OnDataAvailable(_In_opt_ IXMLHTTPRequest2*, _In_opt_ ISequentialStream*) 
    {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnResponseReceived(_In_opt_ IXMLHTTPRequest2*, _In_opt_ ISequentialStream*)
    {
        //auto progress = m_httpTask->m_httpTask._get_impl()->_progress_handler();
        //if (progress && m_httpTask->m_downloaded == 0)
        //{
        //    try { (*progress)(message_direction::download, 0); }
        //    catch (...)
        //    {
        //        m_httpTask->m_exceptionPtr = std::current_exception();
        //    }
        //}

        //if (m_httpTask->m_exceptionPtr != nullptr)
        //    m_httpTask->report_exception(m_httpTask->m_exceptionPtr);
        //else
        //    m_httpTask->complete_request(m_httpTask->m_downloaded);

        // Break the circular reference loop.
        //     - xmlhttp_http_task holds a reference to IXmlHttpRequest2
        //     - IXmlHttpRequest2 holds a reference to HttpRequestCallback
        //     - HttpRequestCallback holds a reference to xmlhttp_http_task
        //
        // Not releasing the winrt_request_context below previously worked due to the
        // implementation of IXmlHttpRequest2, after calling OnError/OnResponseReceived
        // it would immediately release its reference to HttpRequestCallback. However
        // it since has been discovered on Xbox that the implementation is different,
        // the reference to HttpRequestCallback is NOT immediately released and is only
        // done at destruction of IXmlHttpRequest2.
        //
        // To be safe we now will break the circular reference.
        m_httpTask.reset();

        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnError(_In_opt_ IXMLHTTPRequest2*, HRESULT hrError)
    {
        //if (m_httpTask->m_exceptionPtr == nullptr)
        //{
        //    std::wstring msg(L"IXMLHttpRequest2Callback::OnError: ");
        //    msg.append(std::to_wstring(hrError));
        //    msg.append(L": ");
        //    msg.append(utility::conversions::to_string_t(utility::details::windows_category().message(hrError)));
        //    m_httpTask->report_error(hrError, msg);
        //}
        //else
        //{
        //    m_httpTask->report_exception(m_httpTask->m_exceptionPtr);
        //}

        // Break the circular reference loop.
        // See full explanation in OnResponseReceived
        m_httpTask.reset();

        return S_OK;
    }

private:
    std::shared_ptr<xmlhttp_http_task> m_httpTask;
};

class IResponseStream : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ISequentialStream>
{
public:
    IResponseStream(const std::weak_ptr<xmlhttp_http_task>& httpTask)
        : m_httpTask(httpTask)
    { 
    }

    virtual HRESULT STDMETHODCALLTYPE Write(
        _In_reads_bytes_(cb) const void *pv, 
        _In_ ULONG cb, 
        _Out_opt_ ULONG *pcbWritten
        )
    {
        auto httpTask = m_httpTask.lock();
        if (httpTask == nullptr)
        {
            // OnError has already been called so just error out
            return STG_E_CANTSAVE;
        }

        if (pcbWritten != nullptr)
        {
            *pcbWritten = 0;
        }

        if (cb == 0)
        {
            return S_OK;
        }

        try
        {
            errno_t err = httpTask->m_responseBuffer.append(pv, cb);
            if (err)
            {
                return STG_E_CANTSAVE;
            }
            else
            {
                if (pcbWritten != nullptr)
                {
                    *pcbWritten = (ULONG)cb;
                }
                return S_OK;
            }
        }
        catch (...)
        {
            httpTask->m_exceptionPtr = std::current_exception();
            return STG_E_CANTSAVE;
        }
    }

    virtual HRESULT STDMETHODCALLTYPE Read(_Out_writes_bytes_to_(cb, *pcbRead) void *pv, _In_ ULONG cb, _Out_ ULONG *pcbRead)
    {
        UNREFERENCED_PARAMETER(pv);
        UNREFERENCED_PARAMETER(cb);
        UNREFERENCED_PARAMETER(pcbRead);
        return E_NOTIMPL;
    }

private:
    std::weak_ptr<xmlhttp_http_task> m_httpTask;
};


void xmlhttp_http_task::perform_async(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    char buf[10];
    for (int i = 0; i < 10; i++)
    {
        buf[i] = i;
    }

    http_buffer test;
    test.append(buf, 10);

    for (int i = 0; i < 10; i++)
    {
        buf[i] = i + 100;
    }

    test.append(buf, 10);

    for (int i = 0; i < 10; i++)
    {
        buf[i] = i + 200;
    }

    test.append(buf, 10);

    try
    {
        std::string headerName;
        std::string headerValue;
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
            HCHttpCallResponseSetErrorCode(call, hr);
            HCTaskSetCompleted(taskHandle);
            return;
        }

        std::shared_ptr<hc_task> httpTask2 = shared_from_this();
        std::shared_ptr<xmlhttp_http_task> httpTask = std::dynamic_pointer_cast<xmlhttp_http_task>(httpTask2);

        // TODO: convert to wide
        hr = S_OK;
        //hr = m_hRequest->Open(
        //    method,
        //    url,
        //    Microsoft::WRL::Make<HttpRequestCallback>(httpTask).Get(),
        //    nullptr,
        //    nullptr,
        //    nullptr,
        //    nullptr);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to open HTTP request %lu", hr);
            HCHttpCallResponseSetErrorCode(call, hr);
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
                hr = m_hRequest->SetRequestHeader(to_wstring(headerName).c_str(), to_wstring(headerValue).c_str());
            }
        }

        hr = m_hRequest->SetCustomResponseStream(Microsoft::WRL::Make<IResponseStream>(httpTask).Get());
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to set HTTP response stream %lu", hr);
            HCHttpCallResponseSetErrorCode(call, hr);
            HCTaskSetCompleted(taskHandle);
            return;
        }

        hr = m_hRequest->Send(nullptr, 0);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to send HTTP request %lu", hr);
            HCHttpCallResponseSetErrorCode(call, hr);
            HCTaskSetCompleted(taskHandle);
            return;
        }
    }
    catch (std::exception ex)
    {
        HCHttpCallResponseSetErrorCode(call, E_FAIL); // TODO
        HCTaskSetCompleted(taskHandle);
    }
}
