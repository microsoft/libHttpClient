// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "hcwebsocket.h"

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
    _In_ HC_WEBSOCKET_CONNECT_INIT_ARGS args,
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCWebSocketCompletionRoutine completionRoutine
    ) HC_NOEXCEPT
try 
{
    if (uri == nullptr || websocket == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();

    auto connectFunc = httpSingleton->m_websocketConnectFunc;
    if (connectFunc != nullptr)
    {
        try
        {
            connectFunc(uri, websocket, args, taskSubsystemId, taskGroupId, completionRoutineContext, completionRoutine);
            websocket->connectCalled = true;
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
    _In_z_ PCSTR message,
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCWebSocketCompletionRoutine completionRoutine
    ) HC_NOEXCEPT
try
{
    if (message == nullptr || websocket == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();

    auto sendFunc = httpSingleton->m_websocketSendMessageFunc;
    if (sendFunc != nullptr)
    {
        try
        {
            sendFunc(websocket, message, taskSubsystemId, taskGroupId, completionRoutineContext, completionRoutine);
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
    if (websocket == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();

    auto closeFunc = httpSingleton->m_websocketCloseFunc;
    if (closeFunc != nullptr)
    {
        try
        {
            closeFunc(websocket, HC_WEBSOCKET_CLOSE_STATUS::HC_WEBSOCKET_CLOSE_NORMAL);
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

HC_API HC_RESULT HC_CALLING_CONV
HCGlobalGetWebSocketFunctions(
    _Out_ HC_WEBSOCKET_CONNECT_FUNC* websocketConnectFunc,
    _Out_ HC_WEBSOCKET_SEND_MESSAGE_FUNC* websocketSendMessageFunc,
    _Out_ HC_WEBSOCKET_CLOSE_FUNC* websocketCloseFunc
    ) HC_NOEXCEPT
try
{
    if (websocketConnectFunc == nullptr || 
        websocketSendMessageFunc == nullptr ||
        websocketCloseFunc == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    *websocketConnectFunc = httpSingleton->m_websocketConnectFunc;
    *websocketSendMessageFunc = httpSingleton->m_websocketSendMessageFunc;
    *websocketCloseFunc = httpSingleton->m_websocketCloseFunc;

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketGetProxyUri(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _Out_ PCSTR* proxyUri
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || proxyUri == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *proxyUri = websocket->proxyUri.c_str();
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketGetHeader(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
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
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketGetNumHeaders(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || numHeaders == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(websocket->connectHeaders.size());
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketGetHeaderAtIndex(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    ) HC_NOEXCEPT
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    uint32_t index = 0;
    for (auto it = websocket->connectHeaders.cbegin(); it != websocket->connectHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return HC_OK;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketGetFunctions(
    _Out_ HC_WEBSOCKET_MESSAGE_FUNC* messageFunc,
    _Out_ HC_WEBSOCKET_CLOSE_EVENT_FUNC* closeFunc
    ) HC_NOEXCEPT
try
{
    if (messageFunc == nullptr || closeFunc == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    *messageFunc = httpSingleton->m_websocketMessageFunc;
    *closeFunc = httpSingleton->m_websocketCloseEventFunc;

    return HC_OK;
}
CATCH_RETURN()

