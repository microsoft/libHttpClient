// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "hcwebsocket.h"

using namespace xbox::httpclient;


STDAPI 
HCWebSocketCreate(
    _Out_ hc_websocket_handle_t* websocket
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    HC_WEBSOCKET* socket = new HC_WEBSOCKET();
    socket->id = ++httpSingleton->m_lastId;

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCreate [ID %llu]", socket->id);

    *websocket = socket;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketSetProxyUri(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ UTF8CSTR proxyUri
    ) HC_NOEXCEPT
try
{
    RETURN_IF_WEBSOCKET_CONNECT_CALLED(websocket);
    websocket->proxyUri = proxyUri;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketSetHeader(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ UTF8CSTR headerName,
    _In_z_ UTF8CSTR headerValue
    ) HC_NOEXCEPT
try 
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_WEBSOCKET_CONNECT_CALLED(websocket);

    websocket->connectHeaders[headerName] = headerValue;

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketSetHeader [ID %llu]: %s=%s",
        websocket->id, headerName, headerValue);
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketSetFunctions(
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc
    ) HC_NOEXCEPT
try 
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    httpSingleton->m_websocketMessageFunc = messageFunc;
    httpSingleton->m_websocketCloseEventFunc = closeFunc;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketConnect(
    _In_z_ UTF8CSTR uri,
    _In_z_ UTF8CSTR subProtocol,
    _In_ hc_websocket_handle_t websocket,
    _In_ AsyncBlock* asyncBlock
    ) HC_NOEXCEPT
try 
{
    if (uri == nullptr || websocket == nullptr || subProtocol == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    auto connectFunc = httpSingleton->m_websocketConnectFunc;
    if (connectFunc != nullptr)
    {
        try
        {
            websocket->connectCalled = true;
            connectFunc(uri, subProtocol, websocket, asyncBlock);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketConnect [ID %llu]: failed", websocket->id);
        }
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketSendMessage(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ UTF8CSTR message,
    _In_ AsyncBlock* asyncBlock
    ) HC_NOEXCEPT
try
{
    if (message == nullptr || websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    auto sendFunc = httpSingleton->m_websocketSendMessageFunc;
    if (sendFunc != nullptr)
    {
        try
        {
            sendFunc(websocket, message, asyncBlock);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketSendMessage [ID %llu]: failed", websocket->id);
        }
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketDisconnect(
    _In_ hc_websocket_handle_t websocket
    ) HC_NOEXCEPT
try 
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    auto closeEventFunc = httpSingleton->m_websocketCloseEventFunc;
    auto closeFunc = httpSingleton->m_websocketDisconnectFunc;
    if (closeFunc != nullptr)
    {
        try
        {
            HCWebSocketCloseStatus closeStatus = HCWebSocketCloseStatus::HCWebSocketCloseStatus_Normal;
            closeFunc(websocket, closeStatus);

            if(closeEventFunc != nullptr)
            {
                closeEventFunc(websocket, closeStatus);
            }
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketClose [ID %llu]: failed", websocket->id);
        }
    }

    return S_OK;
}
CATCH_RETURN()

hc_websocket_handle_t HCWebSocketDuplicateHandle(
    _In_ hc_websocket_handle_t websocket
    ) HC_NOEXCEPT
try 
{
    if (websocket == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketDuplicateHandle [ID %llu]", websocket->id);
    ++websocket->refCount;

    return websocket;
}
CATCH_RETURN_WITH(nullptr)

STDAPI 
HCWebSocketCloseHandle(
    _In_ hc_websocket_handle_t websocket
    ) HC_NOEXCEPT
try 
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCloseHandle [ID %llu]", websocket->id);
    int refCount = --websocket->refCount;
    if (refCount <= 0)
    {
        ASSERT(refCount == 0); // should only fire at 0
        delete websocket;
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCGlobalSetWebSocketFunctions(
    _In_opt_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_opt_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_opt_ HCWebSocketDisconnectFunction websocketDisconnectFunc
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    httpSingleton->m_websocketConnectFunc = (websocketConnectFunc) ? websocketConnectFunc : Internal_HCWebSocketConnect;
    httpSingleton->m_websocketSendMessageFunc = (websocketSendMessageFunc) ? websocketSendMessageFunc : Internal_HCWebSocketSendMessage;
    httpSingleton->m_websocketDisconnectFunc = (websocketDisconnectFunc) ? websocketDisconnectFunc : Internal_HCWebSocketDisconnect;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCGlobalGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc
    ) HC_NOEXCEPT
try
{
    if (websocketConnectFunc == nullptr || 
        websocketSendMessageFunc == nullptr ||
        websocketDisconnectFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    *websocketConnectFunc = httpSingleton->m_websocketConnectFunc;
    *websocketSendMessageFunc = httpSingleton->m_websocketSendMessageFunc;
    *websocketDisconnectFunc = httpSingleton->m_websocketDisconnectFunc;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketGetProxyUri(
    _In_ hc_websocket_handle_t websocket,
    _Out_ UTF8CSTR* proxyUri
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || proxyUri == nullptr)
    {
        return E_INVALIDARG;
    }

    *proxyUri = websocket->proxyUri.c_str();
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketGetHeader(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ UTF8CSTR headerName,
    _Out_ UTF8CSTR* headerValue
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    auto it = websocket->connectHeaders.find(headerName);
    if (it != websocket->connectHeaders.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketGetNumHeaders(
    _In_ hc_websocket_handle_t websocket,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || numHeaders == nullptr)
    {
        return E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(websocket->connectHeaders.size());
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketGetHeaderAtIndex(
    _In_ hc_websocket_handle_t websocket,
    _In_ uint32_t headerIndex,
    _Out_ UTF8CSTR* headerName,
    _Out_ UTF8CSTR* headerValue
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    uint32_t index = 0;
    for (auto it = websocket->connectHeaders.cbegin(); it != websocket->connectHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return S_OK;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCWebSocketGetFunctions(
    _Out_opt_ HCWebSocketMessageFunction* messageFunc,
    _Out_opt_ HCWebSocketCloseEventFunction* closeFunc
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    if (messageFunc != nullptr)
    {
        *messageFunc = httpSingleton->m_websocketMessageFunc;
    }

    if (closeFunc != nullptr)
    {
        *closeFunc = httpSingleton->m_websocketCloseEventFunc;
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCGetWebSocketConnectResult(
    _In_ AsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) HC_NOEXCEPT
try
{
    return GetAsyncResult(asyncBlock, HCWebSocketConnect, sizeof(WebSocketCompletionResult), result, nullptr);
}
CATCH_RETURN()

STDAPI 
HCGetWebSocketSendMessageResult(
    _In_ AsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) HC_NOEXCEPT
try
{
    return GetAsyncResult(asyncBlock, HCWebSocketSendMessage, sizeof(WebSocketCompletionResult), result, nullptr);
}
CATCH_RETURN()
