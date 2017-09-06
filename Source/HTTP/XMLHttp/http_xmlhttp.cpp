// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if HC_WIN32_API
#include "../httpcall.h"
#include "win/utils_win.h"
#include "singleton.h"
#include <memory.h>

#if !XDK_API
#include <Strsafe.h>
#endif
#if !defined(__WRL_NO_DEFAULT_LIB__)
#define __WRL_NO_DEFAULT_LIB__
#endif
#include <wrl.h>
#if !XDK_API
#include <msxml6.h>
#else
#include <ixmlhttprequest2.h>
#endif

using namespace xbox::httpclient;

class xmlhttp_http_task : public hc_task
{
private:

public:
    xmlhttp_http_task() 
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


// Implementation of IXMLHTTPRequest2Callback.
class HttpRequestCallback : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IXMLHTTPRequest2Callback, Microsoft::WRL::FtmBase>
{
public:
    HttpRequestCallback(const std::shared_ptr<xmlhttp_http_task> &request) : m_request(request)
    {
    }

    // Called when the HTTP request is being redirected to a new URL.
    HRESULT STDMETHODCALLTYPE OnRedirect(_In_opt_ IXMLHTTPRequest2*, __RPC__in_string const WCHAR*) 
    {
        return S_OK;
    }

    // Called when HTTP headers have been received and processed.
    HRESULT STDMETHODCALLTYPE OnHeadersAvailable(_In_ IXMLHTTPRequest2* xmlReq, DWORD dw, __RPC__in_string const WCHAR* phrase)
    {
        //http_response &response = m_request->m_response;
        //response.set_status_code((http::status_code)dw);
        //response.set_reason_phrase(phrase);

        WCHAR* hdrStr = nullptr;
        HRESULT hr = xmlReq->GetAllResponseHeaders(&hdrStr);
        if (SUCCEEDED(hr))
        {
        //    try
        //    {
        //        auto progress = m_request->m_request._get_impl()->_progress_handler();
        //        if (progress && m_request->m_uploaded == 0)
        //        {
        //            (*progress)(message_direction::upload, 0);
        //        }

        //        web::http::details::parse_headers_string(hdrStr, response.headers());
        //        m_request->complete_headers();
        //    }
        //    catch (...)
        //    {
        //        m_request->m_exceptionPtr = std::current_exception();
        //        hr = ERROR_UNHANDLED_EXCEPTION;
        //    }
        }

        if (hdrStr != nullptr)
        {
            ::CoTaskMemFree(hdrStr);
            hdrStr = nullptr;
        }

        return hr;
    }

    // Called when a portion of the entity body has been received.
    HRESULT STDMETHODCALLTYPE OnDataAvailable(_In_opt_ IXMLHTTPRequest2*, _In_opt_ ISequentialStream*) 
    {
        return S_OK;
    }

    // Called when the entire entity response has been received.
    HRESULT STDMETHODCALLTYPE OnResponseReceived(_In_opt_ IXMLHTTPRequest2*, _In_opt_ ISequentialStream*)
    {
        //auto progress = m_request->m_request._get_impl()->_progress_handler();
        //if (progress && m_request->m_downloaded == 0)
        //{
        //    try { (*progress)(message_direction::download, 0); }
        //    catch (...)
        //    {
        //        m_request->m_exceptionPtr = std::current_exception();
        //    }
        //}

        //if (m_request->m_exceptionPtr != nullptr)
        //    m_request->report_exception(m_request->m_exceptionPtr);
        //else
        //    m_request->complete_request(m_request->m_downloaded);

        // Break the circular reference loop.
        //     - winrt_request_context holds a reference to IXmlHttpRequest2
        //     - IXmlHttpRequest2 holds a reference to HttpRequestCallback
        //     - HttpRequestCallback holds a reference to winrt_request_context
        //
        // Not releasing the winrt_request_context below previously worked due to the
        // implementation of IXmlHttpRequest2, after calling OnError/OnResponseReceived
        // it would immediately release its reference to HttpRequestCallback. However
        // it since has been discovered on Xbox that the implementation is different,
        // the reference to HttpRequestCallback is NOT immediately released and is only
        // done at destruction of IXmlHttpRequest2.
        //
        // To be safe we now will break the circular reference.
        m_request.reset();

        return S_OK;
    }

    // Called when an error occurs during the HTTP request.
    HRESULT STDMETHODCALLTYPE OnError(_In_opt_ IXMLHTTPRequest2*, HRESULT hrError)
    {
        //if (m_request->m_exceptionPtr == nullptr)
        //{
        //    std::wstring msg(L"IXMLHttpRequest2Callback::OnError: ");
        //    msg.append(std::to_wstring(hrError));
        //    msg.append(L": ");
        //    msg.append(utility::conversions::to_string_t(utility::details::windows_category().message(hrError)));
        //    m_request->report_error(hrError, msg);
        //}
        //else
        //{
        //    m_request->report_exception(m_request->m_exceptionPtr);
        //}

        // Break the circular reference loop.
        // See full explanation in OnResponseReceived
        m_request.reset();

        return S_OK;
    }

private:
    std::shared_ptr<xmlhttp_http_task> m_request;
};

/// <summary>
/// This class acts as a bridge for the underlying request stream.
/// </summary>
/// <remarks>
/// These operations are completely synchronous, so it's important to block on both
/// read and write operations. The I/O will be done off the UI thread, so there is no risk
/// of causing the UI to become unresponsive.
/// </remarks>
class IRequestStream : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ISequentialStream>
{
public:
    IRequestStream(const std::weak_ptr<xmlhttp_http_task> &context, size_t read_length = SIZE_T_MAX) : 
        m_context(context),
        m_read_length(read_length)
    {
        // read_length is the initial length of the ISequentialStream that is available for read
        // This is required because IXHR2 attempts to read more data that what is specified by
        // the content_length. (Specifically, it appears to be reading 128K chunks regardless of
        // the content_length specified).
    }

    virtual HRESULT STDMETHODCALLTYPE Read(_Out_writes_(cb) void *pv, _In_ ULONG cb, _Out_ ULONG *pcbRead)
    {
        auto context = m_context.lock();
        if (context == nullptr)
        {
            // OnError has already been called so just error out
            return STG_E_READFAULT;
        }

        try
        {
            //auto buffer = context->_get_readbuffer();

            //// Do not read more than the specified read_length
            //msl::safeint3::SafeInt<size_t> safe_count = static_cast<size_t>(cb);
            //size_t size_to_read = safe_count.Min(m_read_length);

            //const size_t count = buffer.getn((uint8_t *)pv, size_to_read).get();
            //*pcbRead = (ULONG)count;
            //if (count == 0 && size_to_read != 0)
            //{
            //    return STG_E_READFAULT;
            //}

            //_ASSERTE(count != static_cast<size_t>(-1));
            //_ASSERTE(m_read_length >= count);
            //m_read_length -= count;

            //auto progress = context->m_request._get_impl()->_progress_handler();
            //if (progress && count > 0)
            //{
            //    context->m_uploaded += count;
            //    try { (*progress)(message_direction::upload, context->m_uploaded); }
            //    catch (...)
            //    {
            //        context->m_exceptionPtr = std::current_exception();
            //        return STG_E_READFAULT;
            //    }
            //}

            return S_OK;
        }
        catch (...)
        {
            //context->m_exceptionPtr = std::current_exception();
            return STG_E_READFAULT;
        }
    }

    virtual HRESULT STDMETHODCALLTYPE Write(_In_reads_bytes_(cb) const void *pv, _In_ ULONG cb, _Out_opt_ ULONG *pcbWritten)
    {
        UNREFERENCED_PARAMETER(pv);
        UNREFERENCED_PARAMETER(cb);
        UNREFERENCED_PARAMETER(pcbWritten);
        return E_NOTIMPL;
    }

private:

    // The request context controls the lifetime of this class so we only hold a weak_ptr.
    std::weak_ptr<xmlhttp_http_task> m_context;

    // Length of the ISequentialStream for reads. This is equivalent
    // to the amount of data that the ISequentialStream is allowed
    // to read from the underlying stream buffer.
    size_t m_read_length;
};

/// <summary>
/// This class acts as a bridge for the underlying response stream.
/// </summary>
/// <remarks>
/// These operations are completely synchronous, so it's important to block on both
/// read and write operations. The I/O will be done off the UI thread, so there is no risk
/// of causing the UI to become unresponsive.
/// </remarks>
class IResponseStream : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ISequentialStream>
{
public:
    IResponseStream(const std::weak_ptr<xmlhttp_http_task>& context)
        : m_context(context)
    { 
    }

    virtual HRESULT STDMETHODCALLTYPE Write(_In_reads_bytes_(cb) const void *pv, _In_ ULONG cb, _Out_opt_ ULONG *pcbWritten)
    {
        auto context = m_context.lock();
        if (context == nullptr)
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
            //auto buffer = context->_get_writebuffer();
            //const size_t count = buffer.putn_nocopy(reinterpret_cast<const uint8_t *>(pv), static_cast<size_t>(cb)).get();

            //_ASSERTE(count != static_cast<size_t>(-1));
            //_ASSERTE(count <= static_cast<size_t>(ULONG_MAX));
            //if (pcbWritten != nullptr)
            //{
            //    *pcbWritten = (ULONG)count;
            //}
            //context->m_downloaded += count;

            //auto progress = context->m_request._get_impl()->_progress_handler();
            //if (progress && count > 0)
            //{
            //    try { (*progress)(message_direction::download, context->m_downloaded); }
            //    catch (...)
            //    {
            //        //context->m_exceptionPtr = std::current_exception();
            //        return STG_E_CANTSAVE;
            //    }
            //}

            return S_OK;
        }
        catch (...)
        {
            //context->m_exceptionPtr = std::current_exception();
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

    // The request context controls the lifetime of this class so we only hold a weak_ptr.
    std::weak_ptr<xmlhttp_http_task> m_context;
};


void xmlhttp_http_task::perform_async(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
)
{
    try
    {
        std::wstring headerName;
        std::wstring headerValue;
        const WCHAR* url = nullptr;
        const WCHAR* method = nullptr;
        const WCHAR* requestBody = nullptr;
        const WCHAR* userAgent = nullptr;
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
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to create IXMLHTTPRequest2 instance %llu", hr);
            return;
        }

        std::shared_ptr<hc_task> httpTask2 = shared_from_this();
        std::shared_ptr<xmlhttp_http_task> httpTask = std::dynamic_pointer_cast<xmlhttp_http_task>(httpTask2);

        hr = m_hRequest->Open(
            method,
            url,
            Microsoft::WRL::Make<HttpRequestCallback>(httpTask).Get(),
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to open HTTP request %llu", hr);
            return;
        }

        hr = m_hRequest->SetProperty(XHR_PROP_NO_CRED_PROMPT, TRUE);

        ULONGLONG timeout = static_cast<ULONGLONG>(timeoutInSeconds * 1000);
        hr = m_hRequest->SetProperty(XHR_PROP_TIMEOUT, timeout);

#ifdef XHR_PROP_ONDATA_NEVER
        hr = m_hRequest->SetProperty(XHR_PROP_ONDATA_THRESHOLD, XHR_PROP_ONDATA_NEVER);
#endif

        hr = m_hRequest->SetRequestHeader(L"User-Agent", L"libHttpClient/1.0.0.0");
        for (uint32_t i = 0; i < numHeaders; i++)
        {
            const WCHAR* headerName;
            const WCHAR* headerValue;
            HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
            if (headerName != nullptr && headerValue != nullptr)
            {
                hr = m_hRequest->SetRequestHeader(headerName, headerValue);
            }
        }

        //hr = winrt_context->m_hRequest->SetCustomResponseStream(Make<IResponseStream>(winrt_context).Get());
        //if (FAILED(hr))
        //{
        //    request->report_error(hr, L"Failure to set HTTP response stream");
        //    return;
        //}

        //if (content_length == 0)
        //{
        //    hr = winrt_context->m_hRequest->Send(nullptr, 0);
        //}
        //else
        //{
        //    if (msg.method() == http::methods::GET || msg.method() == http::methods::HEAD)
        //    {
        //        request->report_exception(http_exception(get_with_body_err_msg));
        //        return;
        //    }

        //    hr = winrt_context->m_hRequest->Send(Make<IRequestStream>(winrt_context, content_length).Get(), content_length);
        //}

        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to send HTTP request %llu", hr);
            return;
        }


        //HttpClient^ httpClient = ref new HttpClient();
        //Uri^ requestUri = ref new Uri(ref new Platform::String(url));
        //HttpRequestMessage^ requestMsg = ref new HttpRequestMessage(ref new HttpMethod(ref new Platform::String(method)), requestUri);

        //requestMsg->Headers->TryAppendWithoutValidation(L"User-Agent", L"libHttpClient/1.0.0.0");

        //for (uint32_t i = 0; i < numHeaders; i++)
        //{
        //    const WCHAR* headerName;
        //    const WCHAR* headerValue;
        //    HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
        //    if (headerName != nullptr && headerValue != nullptr)
        //    {
        //        requestMsg->Headers->TryAppendWithoutValidation(ref new Platform::String(headerName), ref new Platform::String(headerValue));
        //    }
        //}

        //requestMsg->Headers->AcceptEncoding->TryParseAdd(ref new Platform::String(L"gzip"));
        //requestMsg->Headers->AcceptEncoding->TryParseAdd(ref new Platform::String(L"deflate"));
        //requestMsg->Headers->AcceptEncoding->TryParseAdd(ref new Platform::String(L"br"));
        //requestMsg->Headers->Accept->TryParseAdd(ref new Platform::String(L"*/*"));

        //if (requestBody != nullptr)
        //{
        //    requestMsg->Content = ref new HttpStringContent(ref new Platform::String(requestBody));
        //    requestMsg->Content->Headers->ContentType = Windows::Web::Http::Headers::HttpMediaTypeHeaderValue::Parse(L"application/json; charset=utf-8");
        //}

        //m_getHttpAsyncOp = httpClient->SendRequestAsync(requestMsg, HttpCompletionOption::ResponseContentRead);
        //m_getHttpAsyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<HttpResponseMessage^, HttpProgress>(
        //    [call, taskHandle](IAsyncOperationWithProgress<HttpResponseMessage^, HttpProgress>^ asyncOp, AsyncStatus status)
        //{
        //    try
        //    {
        //        std::shared_ptr<uwp_http_task> uwpHttpTask = std::dynamic_pointer_cast<uwp_http_task>(call->task);
        //        uwpHttpTask->m_getHttpAsyncOpStatus = status;
        //        HttpResponseMessage^ httpResponse = asyncOp->GetResults();

        //        uint32_t statusCode = (uint32_t)httpResponse->StatusCode;
        //        HCHttpCallResponseSetStatusCode(call, statusCode);

        //        auto view = httpResponse->Headers->GetView();
        //        auto iter = view->First();
        //        while (iter->MoveNext())
        //        {
        //            auto cur = iter->Current;
        //            auto headerName = cur->Key;
        //            auto headerValue = cur->Value;
        //            HCHttpCallResponseSetHeader(call, headerName->Data(), headerValue->Data());
        //        }

        //        uwpHttpTask->m_readAsStringAsyncOp = httpResponse->Content->ReadAsStringAsync();
        //        uwpHttpTask->m_readAsStringAsyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<Platform::String^, unsigned long long>(
        //            [call, taskHandle](IAsyncOperationWithProgress<Platform::String^, unsigned long long>^ asyncOp, AsyncStatus status)
        //        {
        //            try
        //            {
        //                Platform::String^ httpResponseBody = asyncOp->GetResults();
        //                HCHttpCallResponseSetResponseString(call, httpResponseBody->Data());
        //                HCTaskSetCompleted(taskHandle);
        //            }
        //            catch (Platform::Exception^ ex)
        //            {
        //                HCHttpCallResponseSetErrorCode(call, ex->HResult);
        //                HCTaskSetCompleted(taskHandle);
        //            }
        //        });
        //    }
        //    catch (Platform::Exception^ ex)
        //    {
        //        HCHttpCallResponseSetErrorCode(call, ex->HResult);
        //        HCTaskSetCompleted(taskHandle);
        //    }
        //});

        HCTaskSetCompleted(taskHandle);
    }
    catch (std::exception ex)
    {
        //HCHttpCallResponseSetErrorCode(call, ex.what(). ->HResult);
        HCTaskSetCompleted(taskHandle);
    }
}

#endif