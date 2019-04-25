// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#if !HC_NOWEBSOCKETS

#include "hcwebsocket.h"

using namespace xbox::httpclient;

HC_WEBSOCKET::HC_WEBSOCKET(
    _In_ uint64_t _id,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* functionContext
) :
    id{ _id },
    m_clientMessageFunc{ messageFunc },
    m_clientBinaryMessageFunc{ binaryMessageFunc },
    m_clientCloseEventFunc{ closeFunc },
    m_clientContext{ functionContext }
{
}

HC_WEBSOCKET::~HC_WEBSOCKET()
{
#if !HC_NOWEBSOCKETS
    HC_TRACE_VERBOSE(WEBSOCKET, "HCWebsocketHandle dtor");
#endif
}

void HC_WEBSOCKET::AddClientRef()
{
    ++m_clientRefCount;
    AddRef();
}

void HC_WEBSOCKET::DecClientRef()
{
    if (--m_clientRefCount == 0)
    {
        m_clientMessageFunc = nullptr;
        m_clientBinaryMessageFunc = nullptr;
        m_clientCloseEventFunc = nullptr;

        if (disconnectCallExpected)
        {
            HC_TRACE_WARNING(WEBSOCKET, "No client reference remain for HC_WEBSOCKET but it is either connected/connecting. Disconnecting now.");
            HCWebSocketDisconnect(this);
        }
    }
    DecRef();
}

void HC_WEBSOCKET::AddRef()
{
    if (m_totalRefCount++ == 0)
    {
        m_extraRefHolder = shared_from_this();
    }
}

void HC_WEBSOCKET::DecRef()
{
    if (--m_totalRefCount == 0)
    {
        m_extraRefHolder.reset();
    }
}

void HC_WEBSOCKET::MessageFunc(
    HC_WEBSOCKET* websocket,
    const char* message,
    void* context
)
{
    if (websocket->m_clientMessageFunc)
    {
        try
        {
            websocket->m_clientMessageFunc(websocket, message, websocket->m_clientContext);
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketMessageFunction");
        }
    }
}

void HC_WEBSOCKET::BinaryMessageFunc(
    HC_WEBSOCKET* websocket,
    const uint8_t* bytes,
    uint32_t payloadSize,
    void* context
)
{
    if (websocket->m_clientBinaryMessageFunc)
    {
        try
        {
            websocket->m_clientBinaryMessageFunc(websocket, bytes, payloadSize, websocket->m_clientContext);
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketBinaryMessageFunction");
        }
    }
}

void HC_WEBSOCKET::CloseFunc(
    HC_WEBSOCKET* websocket,
    HCWebSocketCloseStatus status,
    void* context
)
{
    if (websocket->m_clientCloseEventFunc)
    {
        try
        {
            websocket->m_clientCloseEventFunc(websocket, status, websocket->m_clientContext);
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketCloseEventFunction");
        }
    }
    // Release the providers ref
    websocket->disconnectCallExpected = false;
    websocket->DecRef();
}

STDAPI
HCWebSocketCreate(
    _Out_ HCWebsocketHandle* websocket,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* functionContext
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    std::shared_ptr<HC_WEBSOCKET> socket = http_allocate_shared<HC_WEBSOCKET>(
        ++httpSingleton->m_lastId,
        messageFunc,
        binaryMessageFunc,
        closeFunc,
        functionContext
    );

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCreate [ID %llu]", socket->id);

    socket->AddClientRef();
    *websocket = socket.get();
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
    if (websocket == nullptr || proxyUri == nullptr)
    {
        return E_INVALIDARG;
    }
    else if (websocket->disconnectCallExpected)
    {
        return E_HC_CONNECT_ALREADY_CALLED;
    }

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
    else if (websocket->disconnectCallExpected)
    {
        return E_HC_CONNECT_ALREADY_CALLED;
    }

    websocket->connectHeaders[headerName] = headerValue;

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock
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
            // Add a ref for the provider. This guarantees the HC_WEBSOCKET is alive until disconnect.
            websocket->AddRef();
            websocket->disconnectCallExpected = true;
            connectFunc(uri, subProtocol, websocket, asyncBlock, httpSingleton->m_performEnv.get());
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
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept
try
{
    if (message == nullptr || websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

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
HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept
try
{
    if (payloadBytes == nullptr || payloadSize == 0 || websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    auto sendFunc = httpSingleton->m_websocketSendBinaryMessageFunc;
    if (sendFunc != nullptr)
    {
        try
        {
            sendFunc(websocket, payloadBytes, payloadSize, asyncBlock);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketSendBinaryMessageAsync [ID %llu]: failed", websocket->id);
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

    auto disconnectFunc = httpSingleton->m_websocketDisconnectFunc;
    if (disconnectFunc != nullptr)
    {
        try
        {
            websocket->disconnectCallExpected = false;
            disconnectFunc(websocket, HCWebSocketCloseStatus::Normal);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketClose [ID %llu]: failed", websocket->id);
        }
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI_(HCWebsocketHandle) HCWebSocketDuplicateHandle(
    _In_ HCWebsocketHandle websocket
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketDuplicateHandle [ID %llu]", websocket->id);
    websocket->AddClientRef();

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
    websocket->DecClientRef();

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCSetWebSocketFunctions(
    _In_opt_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_opt_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_opt_ HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc,
    _In_opt_ HCWebSocketDisconnectFunction websocketDisconnectFunc
    ) noexcept
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    httpSingleton->m_websocketConnectFunc = (websocketConnectFunc) ? websocketConnectFunc : Internal_HCWebSocketConnectAsync;
    httpSingleton->m_websocketSendMessageFunc = (websocketSendMessageFunc) ? websocketSendMessageFunc : Internal_HCWebSocketSendMessageAsync;
    httpSingleton->m_websocketSendBinaryMessageFunc = (websocketSendBinaryMessageFunc) ? websocketSendBinaryMessageFunc : Internal_HCWebSocketSendBinaryMessageAsync;
    httpSingleton->m_websocketDisconnectFunc = (websocketDisconnectFunc) ? websocketDisconnectFunc : Internal_HCWebSocketDisconnect;

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketSendBinaryMessageFunction* websocketSendBinaryMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc
    ) noexcept
try
{
    if (websocketConnectFunc == nullptr ||
        websocketSendMessageFunc == nullptr ||
        websocketSendBinaryMessageFunc == nullptr ||
        websocketDisconnectFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    *websocketConnectFunc = httpSingleton->m_websocketConnectFunc;
    *websocketSendMessageFunc = httpSingleton->m_websocketSendMessageFunc;
    *websocketSendBinaryMessageFunc = httpSingleton->m_websocketSendBinaryMessageFunc;
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
HCWebSocketGetEventFunctions(
    _In_ HCWebsocketHandle websocket,
    _Out_opt_ HCWebSocketMessageFunction* messageFunc,
    _Out_opt_ HCWebSocketBinaryMessageFunction* binaryMessageFunc,
    _Out_opt_ HCWebSocketCloseEventFunction* closeFunc,
    _Out_ void** context
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    if (messageFunc != nullptr)
    {
        *messageFunc = HC_WEBSOCKET::MessageFunc;
    }

    if (binaryMessageFunc != nullptr)
    {
        *binaryMessageFunc = HC_WEBSOCKET::BinaryMessageFunc;
    }

    if (closeFunc != nullptr)
    {
        *closeFunc = HC_WEBSOCKET::CloseFunc;
    }

    if (context != nullptr)
    {
        *context = nullptr;
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCGetWebSocketConnectResult(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) noexcept
try
{
    return XAsyncGetResult(
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
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) noexcept
try
{
    return XAsyncGetResult(
        asyncBlock,
        reinterpret_cast<void*>(HCWebSocketSendMessageAsync),
        sizeof(WebSocketCompletionResult),
        result,
        nullptr
    );
}
CATCH_RETURN()

#endif

