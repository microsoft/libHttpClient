// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "../httpcall.h"
#include "xmlhttp_http_task.h"
#include "http_request_callback.h"

http_request_callback::http_request_callback(_In_ const std::shared_ptr<xmlhttp_http_task>& httpTask) 
    : m_httpTask(httpTask)
{
}

HRESULT STDMETHODCALLTYPE http_request_callback::OnRedirect(
    _In_opt_ IXMLHTTPRequest2*, 
    __RPC__in_string const WCHAR*)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE http_request_callback::OnHeadersAvailable(
    _In_ IXMLHTTPRequest2* xmlReq, 
    DWORD statusCode, 
    __RPC__in_string const WCHAR* phrase
    )
{
    UNREFERENCED_PARAMETER(phrase);
    m_httpTask->set_status_code(statusCode);

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
            m_httpTask->set_exception(std::current_exception());
            hr = ERROR_UNHANDLED_EXCEPTION;
        }
    }

    if (allResponseHeaders != nullptr)
    {
        ::CoTaskMemFree(allResponseHeaders);
    }

    return hr;
}

HRESULT STDMETHODCALLTYPE http_request_callback::OnDataAvailable(
    _In_opt_ IXMLHTTPRequest2*, 
    _In_opt_ ISequentialStream*
    )
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE http_request_callback::OnResponseReceived(
    _In_opt_ IXMLHTTPRequest2*, 
    _In_opt_ ISequentialStream*
    )
{
    auto call = m_httpTask->call();

    HCHttpCallResponseSetStatusCode(call, m_httpTask->get_status_code());

    auto& headerNames = m_httpTask->get_headers_names();
    auto& headerValues = m_httpTask->get_headers_values();
    ASSERT(headerNames.size() == headerValues.size());
    for (unsigned i = 0; i < headerNames.size(); i++)
    {
        HCHttpCallResponseSetHeader(call, headerNames[i].c_str(), headerValues[i].c_str());
    }

    HRESULT hr = S_OK;
    if (m_httpTask->has_error())
    {
        hr = E_FAIL;
    }
    HCHttpCallResponseSetNetworkErrorCode(call, hr, hr);
    XAsyncComplete(m_httpTask->async_block(), S_OK, 0);

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

HRESULT STDMETHODCALLTYPE http_request_callback::OnError(
    _In_opt_ IXMLHTTPRequest2*, 
    HRESULT hrError
    )
{
    HCHttpCallResponseSetNetworkErrorCode(m_httpTask->call(), E_FAIL, hrError);
    XAsyncComplete(m_httpTask->async_block(), S_OK, 0);

    // Break the circular reference loop.
    // See full explanation in OnResponseReceived
    m_httpTask.reset();

    return S_OK;
}
