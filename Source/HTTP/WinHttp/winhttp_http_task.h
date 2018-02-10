// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include <wrl.h>
#include <winhttp.h>
#if !HC_XDK_API
#include <msxml6.h>
#else
#include <ixmlhttprequest2.h>
#endif
#include "http_buffer.h"
#include "utils.h"
#include "uri.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

enum msg_body_type
{
    no_body,
    content_length_chunked,
    transfer_encoding_chunked
};

class winhttp_http_task : public xbox::httpclient::hc_task
{
public:
    winhttp_http_task(
        _In_ HC_CALL_HANDLE call,
        _In_ HC_TASK_HANDLE taskHandle
        );
    ~winhttp_http_task();

    void perform_async();

    http_internal_vector<http_internal_string> split(
        _In_ const http_internal_string& s,
        _In_z_ const char* delim);
    bool has_error();
    void set_exception(const std::exception_ptr& exceptionPtr);
    http_buffer& response_buffer();
    HC_CALL_HANDLE call();
    HC_TASK_HANDLE task_handle();

private:
    static HRESULT query_header_length(_In_ HC_CALL_HANDLE call, _In_ HINTERNET hRequestHandle, _In_ DWORD header, _Out_ DWORD* pLength);
    static uint32_t parse_status_code(
        _In_ HC_CALL_HANDLE call,
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext);

    static void read_next_response_chunk(_In_ winhttp_http_task* pRequestContext, DWORD bytesRead, bool firstRead = false);
    static void _multiple_segment_write_data(_In_ winhttp_http_task* pRequestContext);

    static void parse_headers_string(_In_ HC_CALL_HANDLE call, _In_ wchar_t* headersStr);

    static void callback_status_request_error(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_sendrequest_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_write_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_headers_available(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_data_available(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_read_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ DWORD statusInfoLength);

    HRESULT send(_In_ const xbox::httpclient::Uri& cUri);

    HRESULT connect(_In_ const xbox::httpclient::Uri& cUri);

    void get_proxy_name(
        _Out_ DWORD* pAccessType,
        _Out_ const wchar_t** pwProxyName
        );

    void get_proxy_info(
        _In_ WINHTTP_PROXY_INFO* pInfo,
        _In_ bool* pProxyInfoRequired,
        _In_ const xbox::httpclient::Uri& cUri);

    static void CALLBACK completion_callback(
        HINTERNET hRequestHandle,
        DWORD_PTR context,
        DWORD statusCode,
        _In_ void* statusInfo,
        DWORD statusInfoLength);

    HC_CALL_HANDLE m_call;
    HC_TASK_HANDLE m_taskHandle;
    http_buffer m_responseBuffer;
    std::exception_ptr m_exceptionPtr;

    HINTERNET m_hSession;
    HINTERNET m_hConnection;
    HINTERNET m_hRequest;
    msg_body_type m_requestBodyType;
    uint64_t m_requestBodyRemainingToWrite;
    uint64_t m_requestBodyOffset;
};


NAMESPACE_XBOX_HTTP_CLIENT_END
