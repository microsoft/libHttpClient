// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#if !HC_NOWEBSOCKETS

#include "hcwebsocket.h"

using namespace xbox::httpclient;


STDAPI
HCWebSocketCreate(
    _Out_ HCWebsocketHandle* websocket
    ) noexcept
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
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* proxyUri
    ) noexcept
try
{
    RETURN_IF_WEBSOCKET_CONNECT_CALLED(websocket);
    websocket->proxyUri = proxyUri;

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketSetHeader(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
    ) noexcept
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_WEBSOCKET_CONNECT_CALLED(websocket);

    websocket->connectHeaders[headerName] = headerValue;

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketSetFunctions(
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc
    ) noexcept
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
HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ AsyncBlock* asyncBlock
    ) noexcept
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
HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ AsyncBlock* asyncBlock
    ) noexcept
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
    _In_ HCWebsocketHandle websocket
    ) noexcept
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

            if (closeEventFunc != nullptr)
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

HCWebsocketHandle HCWebSocketDuplicateHandle(
    _In_ HCWebsocketHandle websocket
    ) noexcept
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
    _In_ HCWebsocketHandle websocket
    ) noexcept
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
HCSetWebSocketFunctions(
    _In_opt_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_opt_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_opt_ HCWebSocketDisconnectFunction websocketDisconnectFunc
    ) noexcept
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    httpSingleton->m_websocketConnectFunc = (websocketConnectFunc) ? websocketConnectFunc : Internal_HCWebSocketConnectAsync;
    httpSingleton->m_websocketSendMessageFunc = (websocketSendMessageFunc) ? websocketSendMessageFunc : Internal_HCWebSocketSendMessageAsync;
    httpSingleton->m_websocketDisconnectFunc = (websocketDisconnectFunc) ? websocketDisconnectFunc : Internal_HCWebSocketDisconnect;

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc
    ) noexcept
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
    _In_ HCWebsocketHandle websocket,
    _Out_ const char** proxyUri
    ) noexcept
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
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
    ) noexcept
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
    _In_ HCWebsocketHandle websocket,
    _Out_ uint32_t* numHeaders
    ) noexcept
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
    _In_ HCWebsocketHandle websocket,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
    ) noexcept
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
    ) noexcept
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
    _Inout_ AsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) noexcept
try
{
    return GetAsyncResult(
        asyncBlock,
        reinterpret_cast<void*>(HCWebSocketConnectAsync),
        sizeof(WebSocketCompletionResult),
        result,
        nullptr
    );
}
CATCH_RETURN()

STDAPI
HCGetWebSocketSendMessageResult(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) noexcept
try
{
    return GetAsyncResult(
        asyncBlock,
        reinterpret_cast<void*>(HCWebSocketSendMessageAsync),
        sizeof(WebSocketCompletionResult),
        result,
        nullptr
    );
}
CATCH_RETURN()

#endif

