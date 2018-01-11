// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "websocket.h"

using namespace xbox::httpclient;


HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketCreate(
    _Out_ HC_WEBSOCKET_HANDLE* websocket
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();

    HC_WEBSOCKET* socket = new HC_WEBSOCKET();
    socket->id = ++httpSingleton->m_lastId;

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCreate [ID %llu]", socket->id);

    *websocket = socket;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSetProxyUri(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR proxyUri
    ) HC_NOEXCEPT
try
{
    RETURN_IF_WEBSOCKET_CONNECT_CALLED(websocket);
    websocket->proxyUri = proxyUri;

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSetHeader(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR headerName,
    _In_z_ PCSTR headerValue
    ) HC_NOEXCEPT
try 
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }
    RETURN_IF_WEBSOCKET_CONNECT_CALLED(websocket);

    websocket->connectHeaders[headerName] = headerValue;

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketSetHeader [ID %llu]: %s=%s",
        websocket->id, headerName, headerValue);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSetFunctions(
    _In_opt_ HC_WEBSOCKET_MESSAGE_FUNC messageFunc,
    _In_opt_ HC_WEBSOCKET_CLOSE_EVENT_FUNC closeFunc
    ) HC_NOEXCEPT
try 
{
    auto httpSingleton = get_http_singleton();
    httpSingleton->m_websocketMessageFunc = messageFunc;
    httpSingleton->m_websocketCloseEventFunc = closeFunc;

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CONNECT_INIT_ARGS args
    ) HC_NOEXCEPT
try 
{
    auto httpSingleton = get_http_singleton();

    auto connectFunc = httpSingleton->m_websocketConnectFunc;
    if (connectFunc != nullptr)
    {
        try
        {
            connectFunc(uri, websocket, args);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketConnect [ID %llu]: failed", websocket->id);
        }
    }

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton();

    auto sendFunc = httpSingleton->m_websocketSendMessageFunc;
    if (sendFunc != nullptr)
    {
        try
        {
            sendFunc(websocket, message);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketSendMessage [ID %llu]: failed", websocket->id);
        }
    }

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketClose(
    _In_ HC_WEBSOCKET_HANDLE websocket
    ) HC_NOEXCEPT
try 
{
    auto httpSingleton = get_http_singleton();

    auto closeFunc = httpSingleton->m_websocketCloseFunc;
    if (closeFunc != nullptr)
    {
        try
        {
            closeFunc(websocket);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketClose [ID %llu]: failed", websocket->id);
        }
    }

    return HC_OK;
}
CATCH_RETURN()

HC_WEBSOCKET_HANDLE HCWebSocketDuplicateHandle(
    _In_ HC_WEBSOCKET_HANDLE websocket
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

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketCloseHandle(
    _In_ HC_WEBSOCKET_HANDLE websocket
    ) HC_NOEXCEPT
try 
{
    if (websocket == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCloseHandle [ID %llu]", websocket->id);
    int refCount = --websocket->refCount;
    if (refCount <= 0)
    {
        assert(refCount == 0); // should only fire at 0
        delete websocket;
    }

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCGlobalSetWebSocketFunctions(
    _In_opt_ HC_WEBSOCKET_CONNECT_FUNC websocketConnectFunc,
    _In_opt_ HC_WEBSOCKET_SEND_MESSAGE_FUNC websocketSendMessageFunc,
    _In_opt_ HC_WEBSOCKET_CLOSE_FUNC websocketCloseFunc
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton();
    httpSingleton->m_websocketConnectFunc = (websocketConnectFunc) ? websocketConnectFunc : Internal_HCWebSocketConnect;
    httpSingleton->m_websocketSendMessageFunc = (websocketSendMessageFunc) ? websocketSendMessageFunc : Internal_HCWebSocketSendMessage;
    httpSingleton->m_websocketCloseFunc = (websocketCloseFunc) ? websocketCloseFunc : Internal_HCWebSocketClose;

    return HC_OK;
}
CATCH_RETURN()

HC_RESULT Internal_HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CONNECT_INIT_ARGS args
    )
{
    // TODO
    return HC_OK;
}

HC_RESULT Internal_HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message
    )
{
    // TODO
    return HC_OK;
}

HC_RESULT Internal_HCWebSocketClose(
    _In_ HC_WEBSOCKET_HANDLE websocket
    )
{
    // TODO
    return HC_OK;
}

