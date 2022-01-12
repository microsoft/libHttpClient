// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include <wrl.h>
#if !HC_XDK_API
#include <msxml6.h>
#else
#include <ixmlhttprequest2.h>
#endif
#include "utils.h"

class xmlhttp_http_task : public xbox::httpclient::hc_task
{
public:
    xmlhttp_http_task(
        _Inout_ XAsyncBlock* asyncBlock,
        _In_ HCCallHandle call
        );
    ~xmlhttp_http_task();

    static void CALLBACK PerformAsyncHandler(
        HCCallHandle callHandle,
        XAsyncBlock* async,
        void* context,
        HCPerformEnv env
    ) noexcept;

    void perform_async(
        _Inout_ XAsyncBlock* asyncBlock,
        _In_ HCCallHandle call
        );

    void set_status_code(_In_ uint32_t statusCode);
    uint32_t get_status_code();
    http_internal_vector<http_internal_string> split(
        _In_ const http_internal_string& s,
        _In_z_ const char* delim);
    void set_headers(_In_ WCHAR* allResponseHeaders);
    const http_internal_vector<http_internal_string>& get_headers_names();
    const http_internal_vector<http_internal_string>& get_headers_values();
    bool has_error();
    void set_exception(const std::exception_ptr& exceptionPtr);
    HCCallHandle call();
    XAsyncBlock* async_block();

private:
    HCCallHandle m_call;
    XAsyncBlock* m_asyncBlock;
    uint32_t m_statusCode;
    std::exception_ptr m_exceptionPtr;
    HRESULT m_hrCoInit;
    http_internal_vector<http_internal_string> m_headerNames;
    http_internal_vector<http_internal_string> m_headerValues;
    Microsoft::WRL::ComPtr<IXMLHTTPRequest2> m_hRequest;
};

