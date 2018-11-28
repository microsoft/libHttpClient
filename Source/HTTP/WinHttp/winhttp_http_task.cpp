// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include <winhttp.h>
#if !HC_XDK_API
#include <Shlwapi.h>
#endif
#include "../httpcall.h"
#include "uri.h"
#include "winhttp_http_task.h"

#define CRLF L"\r\n"

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

winhttp_http_task::winhttp_http_task(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    ) :
    m_call(call),
    m_asyncBlock(asyncBlock),
    m_hSession(nullptr),
    m_hConnection(nullptr),
    m_hRequest(nullptr),
    m_requestBodyType(msg_body_type::no_body),
    m_requestBodyRemainingToWrite(0),
    m_requestBodyOffset(0),
    m_proxyType(proxy_type::default_proxy)
{
}

winhttp_http_task::~winhttp_http_task()
{
    if (m_hSession != nullptr) WinHttpCloseHandle(m_hSession);
    if (m_hConnection != nullptr) WinHttpCloseHandle(m_hConnection);
}

void winhttp_http_task::complete_task(_In_ HRESULT translatedHR)
{
    complete_task(translatedHR, translatedHR);
}

void winhttp_http_task::complete_task(_In_ HRESULT translatedHR, uint32_t platformSpecificError)
{
    HCHttpCallResponseSetNetworkErrorCode(m_call, translatedHR, platformSpecificError);
    CompleteAsync(m_asyncBlock, S_OK, 0);
    HCHttpCallSetContext(m_call, nullptr);
    WinHttpSetStatusCallback(m_hSession, nullptr, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, NULL);
    shared_ptr_cache::remove<winhttp_http_task>(this);
}

// Helper function to query/read next part of response data from winhttp.
void winhttp_http_task::read_next_response_chunk(_In_ winhttp_http_task* pRequestContext, DWORD bytesRead)
{
    if (!WinHttpQueryDataAvailable(pRequestContext->m_hRequest, nullptr))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryDataAvailable errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
    }
}

void winhttp_http_task::_multiple_segment_write_data(_In_ winhttp_http_task* pRequestContext)
{
    const uint64_t defaultChunkSize = 64 * 1024;
    uint64_t safeSize = std::min(pRequestContext->m_requestBodyRemainingToWrite, defaultChunkSize);

    const BYTE* requestBody = nullptr;
    uint32_t requestBodyBytes = 0;
    if (HCHttpCallRequestGetRequestBodyBytes(pRequestContext->m_call, &requestBody, &requestBodyBytes) != S_OK)
    {
        return;
    }

    if( !WinHttpWriteData(
        pRequestContext->m_hRequest,
        &requestBody[pRequestContext->m_requestBodyOffset],
        static_cast<DWORD>(safeSize),
        nullptr))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpWriteData errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }

    // Stop writing chunks after this one if no more data.
    pRequestContext->m_requestBodyRemainingToWrite -= safeSize;
    if (pRequestContext->m_requestBodyRemainingToWrite == 0)
    {
        pRequestContext->m_requestBodyType = msg_body_type::no_body;
    }
    pRequestContext->m_requestBodyOffset += safeSize;
}

void winhttp_http_task::callback_status_write_complete(
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext,
    _In_ void* statusInfo)
{
    DWORD bytesWritten = *((DWORD *)statusInfo);
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE bytesWritten=%d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), bytesWritten);

    if (pRequestContext->m_requestBodyType == content_length_chunked)
    {
        _multiple_segment_write_data(pRequestContext);
    }
    else
    {
        if (!WinHttpReceiveResponse(hRequestHandle, nullptr))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpReceiveResponse errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), dwError);
            pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
            return;
        }
    }
}

void winhttp_http_task::callback_status_request_error(
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext,
    _In_ void* statusInfo)
{
    WINHTTP_ASYNC_RESULT *error_result = reinterpret_cast<WINHTTP_ASYNC_RESULT *>(statusInfo);
    if (error_result == nullptr)
        return;

    const DWORD errorCode = error_result->dwError;
    HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_REQUEST_ERROR dwResult=%d dwError=%d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), error_result->dwResult, error_result->dwError);
    pRequestContext->complete_task(E_FAIL, errorCode);
}

void winhttp_http_task::callback_status_sendrequest_complete(
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext,
    _In_ void* statusInfo)
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId() );

    if (pRequestContext->m_requestBodyType == content_length_chunked)
    {
        _multiple_segment_write_data(pRequestContext);
    }
    else
    {
        if (!WinHttpReceiveResponse(hRequestHandle, nullptr))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpReceiveResponse errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), dwError);
            pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
            return;
        }
    }
}

HRESULT winhttp_http_task::query_header_length(
    _In_ hc_call_handle_t call,
    _In_ HINTERNET hRequestHandle,
    _In_ DWORD header,
    _Out_ DWORD* pLength)
{
    if (!WinHttpQueryHeaders(
        hRequestHandle,
        header,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        pLength,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD dwError = GetLastError();
        if (dwError != ERROR_INSUFFICIENT_BUFFER)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryHeaders errorcode %d", HCHttpCallGetId(call), GetCurrentThreadId(), dwError);
            return E_FAIL;
        }
    }

    return S_OK;
}

uint32_t winhttp_http_task::parse_status_code(
    _In_ hc_call_handle_t call,
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext
    )
{
    DWORD length = 0;
    HRESULT hr = query_header_length(pRequestContext->m_call, hRequestHandle, WINHTTP_QUERY_STATUS_CODE, &length);
    if (FAILED(hr))
    {
        pRequestContext->complete_task(E_FAIL, hr);
        return 0;
    }

    http_internal_wstring buffer;
    buffer.resize(length);

    if (!WinHttpQueryHeaders(
        hRequestHandle,
        WINHTTP_QUERY_STATUS_CODE,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &buffer[0],
        &length,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryHeaders errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return 0;
    }

    uint32_t statusCode = static_cast<uint32_t>(_wtoi(buffer.c_str()));
    HCHttpCallResponseSetStatusCode(call, statusCode);

    return statusCode;
}


void winhttp_http_task::parse_headers_string(
    _In_ hc_call_handle_t call,
    _In_ wchar_t* headersStr)
{
    wchar_t* context = nullptr;
    wchar_t* line = wcstok_s(headersStr, CRLF, &context);
    while (line != nullptr)
    {
        http_internal_wstring header_line(line);
        const size_t colonIndex = header_line.find_first_of(L":");
        if (colonIndex != http_internal_wstring::npos)
        {
            http_internal_wstring key = header_line.substr(0, colonIndex);
            http_internal_wstring value = header_line.substr(colonIndex + 1, header_line.length() - colonIndex - 1);
            trim_whitespace(key);
            trim_whitespace(value);

            http_internal_string aKey = utf8_from_utf16(key);
            http_internal_string aValue = utf8_from_utf16(value);
            HCHttpCallResponseSetHeader(call, aKey.c_str(), aValue.c_str());
        }
        line = wcstok_s(nullptr, CRLF, &context);
    }
}

void winhttp_http_task::callback_status_headers_available(
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext,
    _In_ void* statusInfo)
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId() );

    // First need to query to see what the headers size is.
    DWORD headerBufferLength = 0;
    HRESULT hr = query_header_length(pRequestContext->m_call, hRequestHandle, WINHTTP_QUERY_RAW_HEADERS_CRLF, &headerBufferLength);
    if (FAILED(hr))
    {
        pRequestContext->complete_task(E_FAIL, hr);
        return;
    }

    // Now allocate buffer for headers and query for them.
    http_internal_vector<unsigned char> header_raw_buffer;
    header_raw_buffer.resize(headerBufferLength);
    wchar_t* headerBuffer = reinterpret_cast<wchar_t*>(&header_raw_buffer[0]);
    if (!WinHttpQueryHeaders(
        hRequestHandle,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        headerBuffer,
        &headerBufferLength,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryHeaders errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }

    uint32_t statusCode = parse_status_code(pRequestContext->m_call, hRequestHandle, pRequestContext);
    parse_headers_string(pRequestContext->m_call, headerBuffer);
    read_next_response_chunk(pRequestContext, 0);
}

void winhttp_http_task::callback_status_data_available(
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext,
    _In_ void* statusInfo)
{
    // Status information contains pointer to DWORD containing number of bytes available.
    DWORD newBytesAvailable = *(PDWORD)statusInfo;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE newBytesAvailable=%d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), newBytesAvailable);

    if (newBytesAvailable > 0)
    {
        size_t oldSize = pRequestContext->m_responseBuffer.size();
        size_t newSize = oldSize + newBytesAvailable;
        pRequestContext->m_responseBuffer.resize(newSize);

        // Read in body all at once.
        if (!WinHttpReadData(
            hRequestHandle,
            &pRequestContext->m_responseBuffer[oldSize],
            newBytesAvailable,
            nullptr))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpReadData errorcode %d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), GetLastError());
            pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
            return;
        }
    }
    else
    {
        // No more data available, complete the request.
        {
            if (pRequestContext->m_responseBuffer.size() > 0)
            {
                HCHttpCallResponseSetResponseBodyBytes(pRequestContext->m_call,
                    pRequestContext->m_responseBuffer.data(),
                    pRequestContext->m_responseBuffer.size()
                );
            }
        }

        pRequestContext->complete_task(S_OK);
    }
}

void winhttp_http_task::callback_status_read_complete(
    _In_ HINTERNET hRequestHandle,
    _In_ winhttp_http_task* pRequestContext,
    _In_ DWORD statusInfoLength)
{
    // Status information length contains the number of bytes read.
    const DWORD bytesRead = statusInfoLength;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_READ_COMPLETE bytesRead=%d", HCHttpCallGetId(pRequestContext->m_call), GetCurrentThreadId(), bytesRead);

    // If no bytes have been read, then this is the end of the response.
    if (bytesRead == 0)
    {
        if (pRequestContext->m_responseBuffer.size() > 0)
        {
            HCHttpCallResponseSetResponseBodyBytes(pRequestContext->m_call,
                pRequestContext->m_responseBuffer.data(),
                pRequestContext->m_responseBuffer.size()
            );
        }
        pRequestContext->complete_task(S_OK);
    }
    else
    {
        read_next_response_chunk(pRequestContext, bytesRead);
    }
}


static std::string HttpCallbackStatusCodeToString(DWORD statusCode)
{
    switch (statusCode)
    {
    case WINHTTP_CALLBACK_STATUS_RESOLVING_NAME: return "WINHTTP_CALLBACK_STATUS_RESOLVING_NAME";
    case WINHTTP_CALLBACK_STATUS_NAME_RESOLVED: return "WINHTTP_CALLBACK_STATUS_NAME_RESOLVED";
    case WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER: return "WINHTTP_CALLBACK_STATUS_CONNECTING_TO_SERVER";
    case WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER: return "WINHTTP_CALLBACK_STATUS_CONNECTED_TO_SERVER";
    case WINHTTP_CALLBACK_STATUS_SENDING_REQUEST: return "WINHTTP_CALLBACK_STATUS_SENDING_REQUEST";
    case WINHTTP_CALLBACK_STATUS_REQUEST_SENT: return "WINHTTP_CALLBACK_STATUS_REQUEST_SENT";
    case WINHTTP_CALLBACK_STATUS_RECEIVING_RESPONSE: return "WINHTTP_CALLBACK_STATUS_RECEIVING_RESPONSE";
    case WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED: return "WINHTTP_CALLBACK_STATUS_RESPONSE_RECEIVED";
    case WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION: return "WINHTTP_CALLBACK_STATUS_CLOSING_CONNECTION";
    case WINHTTP_CALLBACK_STATUS_CONNECTION_CLOSED: return "WINHTTP_CALLBACK_STATUS_CONNECTION_CLOSED";
    case WINHTTP_CALLBACK_STATUS_HANDLE_CREATED: return "WINHTTP_CALLBACK_STATUS_HANDLE_CREATED";
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING: return "WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING";
    case WINHTTP_CALLBACK_STATUS_DETECTING_PROXY: return "WINHTTP_CALLBACK_STATUS_DETECTING_PROXY";
    case WINHTTP_CALLBACK_STATUS_REDIRECT: return "WINHTTP_CALLBACK_STATUS_REDIRECT";
    case WINHTTP_CALLBACK_STATUS_INTERMEDIATE_RESPONSE: return "WINHTTP_CALLBACK_STATUS_INTERMEDIATE_RESPONSE";
    case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE: return "WINHTTP_CALLBACK_STATUS_SECURE_FAILURE";
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE: return "WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE";
    case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE: return "WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE";
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE: return "WINHTTP_CALLBACK_STATUS_READ_COMPLETE";
    case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE: return "WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE";
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR: return "WINHTTP_CALLBACK_STATUS_REQUEST_ERROR";
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE: return "WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE";
    case WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE: return "WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE";
    case WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE: return "WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE";
    case WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE: return "WINHTTP_CALLBACK_STATUS_SHUTDOWN_COMPLETE";
    default: return "Unknown";
    }
}

void CALLBACK winhttp_http_task::completion_callback(
    HINTERNET hRequestHandle,
    DWORD_PTR context,
    DWORD statusCode,
    _In_ void* statusInfo,
    DWORD statusInfoLength)
{
    // Callback used with WinHTTP to listen for async completions.
    UNREFERENCED_PARAMETER(statusInfoLength);
    if (statusCode == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
        return;

    // Fetch std::shared_ptr if its not removed from cache. 
    // If its removed from cache, then error happened and we can ignore incoming calls
    auto requestContext = shared_ptr_cache::fetch<winhttp_http_task>(reinterpret_cast<void*>(context), false, false);
    if (requestContext == nullptr)
        return;

    try
    {
        // The std::shared_ptr of requestContext will keep the object alive during this function 
        // even if its removed from shared_ptr_cache
        winhttp_http_task* pRequestContext = requestContext.get();

        // Process 1 thread at a time since updating shared state
        win32_cs_autolock autoCriticalSection(&pRequestContext->m_lock);

        if (shared_ptr_cache::fetch<winhttp_http_task>(reinterpret_cast<void*>(context), false, false) == nullptr)
        {
            return;
        }

        switch (statusCode)
        {
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            {
                callback_status_request_error(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            {
                callback_status_sendrequest_complete(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            {
                callback_status_headers_available(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
            {
                callback_status_data_available(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            {
                callback_status_read_complete(hRequestHandle, pRequestContext, statusInfoLength);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
            {
                callback_status_write_complete(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            default:
            {
                HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] %s", HttpCallbackStatusCodeToString(statusCode).c_str(), GetCurrentThreadId());
                break;
            }
        }
    }
    catch (std::bad_alloc const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::bad_alloc in completion_callback: %s", E_OUTOFMEMORY, e.what());
        requestContext->complete_task(E_OUTOFMEMORY);
    }
    catch (std::exception const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception in completion_callback: %s", E_FAIL, e.what());
        requestContext->complete_task(E_FAIL);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception in completion_callback", E_FAIL);
        requestContext->complete_task(E_FAIL);
    }
}

void winhttp_http_task::get_proxy_name(
    _Out_ DWORD* pAccessType,
    _Out_ const wchar_t** pwProxyName)
{
    switch (m_proxyType)
    {
        case proxy_type::no_proxy:
        {
            *pAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
            *pwProxyName = WINHTTP_NO_PROXY_NAME;
            break;
        }

        case proxy_type::named_proxy:
        {
            *pAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;

            http_internal_wstring wProxyHost = utf16_from_utf8(m_proxyUri.Host());

            // WinHttpOpen cannot handle trailing slash in the name, so here is some string gymnastics to keep WinHttpOpen happy
            if (m_proxyUri.IsPortDefault())
            {
                m_wProxyName = wProxyHost;
                *pwProxyName = m_wProxyName.c_str();
            }
            else
            {
                if (m_proxyUri.Port() > 0)
                {
                    http_internal_basic_stringstream<wchar_t> ss;
                    ss.imbue(std::locale::classic());
                    ss << wProxyHost << L":" << m_proxyUri.Port();
                    m_wProxyName = ss.str();
                    *pwProxyName = m_wProxyName.c_str();
                }
                else
                {
                    m_wProxyName = wProxyHost;
                    *pwProxyName = m_wProxyName.c_str();
                }
            }
            break;
        }

        default:
        case proxy_type::autodiscover_proxy:
        case proxy_type::default_proxy:
        {
            *pAccessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
            *pwProxyName = WINHTTP_NO_PROXY_NAME;
            break;
        }
    }
}

void winhttp_http_task::set_autodiscover_proxy(
    _In_ const xbox::httpclient::Uri& cUri)
{
    WINHTTP_PROXY_INFO info = { 0 };

    WINHTTP_AUTOPROXY_OPTIONS autoproxy_options;
    memset(&autoproxy_options, 0, sizeof(WINHTTP_AUTOPROXY_OPTIONS));
    memset(&info, 0, sizeof(WINHTTP_PROXY_INFO));

    autoproxy_options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
    autoproxy_options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    autoproxy_options.fAutoLogonIfChallenged = TRUE;

    auto result = WinHttpGetProxyForUrl(
        m_hSession,
        utf16_from_utf8(cUri.FullPath()).c_str(),
        &autoproxy_options,
        &info);
    if (result)
    {
        auto result = WinHttpSetOption(
            m_hRequest,
            WINHTTP_OPTION_PROXY,
            &info,
            sizeof(WINHTTP_PROXY_INFO));
        if (!result)
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpSetOption errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        }
    }
    else
    {
        // Failure to download the auto-configuration script is not fatal. Fall back to the default proxy.
    }
}

HRESULT winhttp_http_task::connect(
    _In_ const xbox::httpclient::Uri& cUri
    )
{
    const char* url = nullptr;
    const char* method = nullptr;
    HRESULT hr = HCHttpCallRequestGetUrl(m_call, &method, &url);
    if( FAILED(hr) )
    {
        return hr;
    }

    m_proxyType = get_ie_proxy_info(cUri.IsSecure() ? proxy_protocol::https : proxy_protocol::http, m_proxyUri);

    DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    const wchar_t* wProxyName = nullptr;
    get_proxy_name(&accessType, &wProxyName);

    m_hSession = WinHttpOpen(
        NULL,
        accessType,
        wProxyName,
        WINHTTP_NO_PROXY_BYPASS,
        WINHTTP_FLAG_ASYNC);
    if (!m_hSession)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpOpen errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    uint32_t timeoutInSeconds = 0;
    hr = HCHttpCallRequestGetTimeout(m_call, &timeoutInSeconds);
    if (FAILED(hr))
    {
        return hr;
    }

    int timeoutInMilliseconds = static_cast<int>(timeoutInSeconds * 1000);
    if (!WinHttpSetTimeouts(
        m_hSession,
        timeoutInMilliseconds,
        timeoutInMilliseconds,
        timeoutInMilliseconds,
        timeoutInMilliseconds))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpSetTimeouts errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    if (WINHTTP_INVALID_STATUS_CALLBACK == WinHttpSetStatusCallback(
        m_hSession,
        &winhttp_http_task::completion_callback,
        WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
        0))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpSetStatusCallback errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    unsigned int port = cUri.IsPortDefault() ?
        (cUri.IsSecure() ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT) :
        cUri.Port();
    http_internal_wstring wUrlHost = utf16_from_utf8(cUri.Host());

    m_hConnection = WinHttpConnect(
        m_hSession,
        wUrlHost.c_str(),
        (INTERNET_PORT)port,
        0);
    if (m_hConnection == nullptr)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpConnect errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    return S_OK;
}

http_internal_wstring flatten_http_headers(_In_ hc_call_handle_t call)
{
    http_internal_wstring flattened_headers;

    bool foundUserAgent = false;
    uint32_t numHeaders = 0;
    HCHttpCallRequestGetNumHeaders(call, &numHeaders);
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* iHeaderName;
        const char* iHeaderValue;
        HCHttpCallRequestGetHeaderAtIndex(call, i, &iHeaderName, &iHeaderValue);
        if (iHeaderName != nullptr && iHeaderValue != nullptr)
        {
            auto wHeaderName = utf16_from_utf8(iHeaderName);
            if (wHeaderName == L"User-Agent")
            {
                foundUserAgent = true;
            }

            flattened_headers.append(wHeaderName);
            flattened_headers.push_back(L':');
            flattened_headers.append(utf16_from_utf8(iHeaderValue));
            flattened_headers.append(CRLF);
        }
    }

    if (!foundUserAgent)
    {
        flattened_headers.append(L"User-Agent:libHttpClient/1.0.0.0\r\n");
    }

    return flattened_headers;
}

HRESULT winhttp_http_task::send(
    _In_ const xbox::httpclient::Uri& cUri
    )
{
    const char* url = nullptr;
    const char* method = nullptr;
    HRESULT hr = HCHttpCallRequestGetUrl(m_call, &method, &url);
    if( FAILED(hr) )
    {
        return hr;
    }
    
    // Need to form uri path, query, and fragment for this request.
    http_internal_wstring wEncodedResource = utf16_from_utf8(cUri.Resource());
    http_internal_wstring wMethod = utf16_from_utf8(method);

    // Open the request.
    m_hRequest = WinHttpOpenRequest(
        m_hConnection,
        wMethod.c_str(),
        wEncodedResource.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_ESCAPE_DISABLE | (cUri.IsSecure() ? WINHTTP_FLAG_SECURE : 0));
    if (m_hRequest == nullptr)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpOpenRequest errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    if (m_proxyType == proxy_type::autodiscover_proxy)
    {
        set_autodiscover_proxy(cUri);
    }

    const BYTE* requestBody = nullptr;
    uint32_t requestBodyBytes = 0;
    hr = HCHttpCallRequestGetRequestBodyBytes(m_call, &requestBody, &requestBodyBytes);
    if (FAILED(hr))
    {
        return hr;
    }

    if (requestBodyBytes > 0)
    {
        // While we won't be transfer-encoding the data, we will write it in portions.
        m_requestBodyType = msg_body_type::content_length_chunked;
        m_requestBodyRemainingToWrite = requestBodyBytes;
    }
    else
    {
        m_requestBodyType = msg_body_type::no_body;
        m_requestBodyRemainingToWrite = 0;
    }

    uint32_t numHeaders = 0;
    hr = HCHttpCallRequestGetNumHeaders(m_call, &numHeaders);
    if (FAILED(hr))
    {
        return hr;
    }

    if (numHeaders > 0)
    {
        http_internal_wstring flattenedHeaders = flatten_http_headers(m_call);
        if (!WinHttpAddRequestHeaders(
                m_hRequest,
                flattenedHeaders.c_str(),
                static_cast<DWORD>(flattenedHeaders.length()),
                WINHTTP_ADDREQ_FLAG_ADD))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpAddRequestHeaders errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
            return HRESULT_FROM_WIN32(dwError);
        }
    }

    DWORD dwTotalLength = 0;
    switch (m_requestBodyType)
    {
        case msg_body_type::no_body: dwTotalLength = 0; break;
        case msg_body_type::content_length_chunked: dwTotalLength = (DWORD)requestBodyBytes; break;
        default: dwTotalLength = WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH; break;
    }

    if (!WinHttpSendRequest(
        m_hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        nullptr,
        0,
        dwTotalLength,
        (DWORD_PTR)this))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpSendRequest errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    return S_OK;
}


void winhttp_http_task::perform_async()
{
    try
    {
        const char* url = nullptr;
        const char* method = nullptr;
        HCHttpCallRequestGetUrl(m_call, &method, &url);
        xbox::httpclient::Uri cUri(url);

        HRESULT hr = connect(cUri);
        if (SUCCEEDED(hr))
        {
            hr = send(cUri);
        }

        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Failure to send HTTP request 0x%0.8x", hr);
            complete_task(E_FAIL, hr);
            return;
        }
    }
    catch (std::bad_alloc const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::bad_alloc in winhttp_http_task: %s", E_OUTOFMEMORY, e.what());
        complete_task(E_OUTOFMEMORY);
    }
    catch (std::exception const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception in winhttp_http_task: %s", E_FAIL, e.what());
        complete_task(E_FAIL);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception in winhttp_http_task", E_FAIL);
        complete_task(E_FAIL);
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_END

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
    std::shared_ptr<xbox::httpclient::winhttp_http_task> httpTask = http_allocate_shared<winhttp_http_task>(asyncBlock, call);
    shared_ptr_cache::store<winhttp_http_task>(httpTask);
    HCHttpCallSetContext(call, httpTask.get());
    httpTask->perform_async();
}

