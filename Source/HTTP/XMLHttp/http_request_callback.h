// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "../httpcall.h"
#include "xmlhttp_http_task.h"

class http_request_callback : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IXMLHTTPRequest2Callback, Microsoft::WRL::FtmBase>
{
public:
    http_request_callback(_In_ const std::shared_ptr<xmlhttp_http_task>& httpTask);
    HRESULT STDMETHODCALLTYPE OnRedirect(_In_opt_ IXMLHTTPRequest2*, __RPC__in_string const WCHAR*);
    HRESULT STDMETHODCALLTYPE OnHeadersAvailable(_In_ IXMLHTTPRequest2* xmlReq, DWORD statusCode, __RPC__in_string const WCHAR* phrase);
    HRESULT STDMETHODCALLTYPE OnDataAvailable(_In_opt_ IXMLHTTPRequest2*, _In_opt_ ISequentialStream*);
    HRESULT STDMETHODCALLTYPE OnResponseReceived(_In_opt_ IXMLHTTPRequest2*, _In_opt_ ISequentialStream*);
    HRESULT STDMETHODCALLTYPE OnError(_In_opt_ IXMLHTTPRequest2*, HRESULT hrError);

private:
    std::shared_ptr<xmlhttp_http_task> m_httpTask;
};
