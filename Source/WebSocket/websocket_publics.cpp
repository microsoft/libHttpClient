#include "pch.h"
#include "hcwebsocket.h"

using namespace xbox::httpclient;

#if !HC_NOWEBSOCKETS

STDAPI HCWebSocketCreate(
    _Out_ HCWebsocketHandle* handle,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* functionContext
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle);

    auto createWebSocketResult = WebSocket::Initialize();
    RETURN_IF_FAILED(createWebSocketResult.hr);

    *handle = HC_WEBSOCKET_OBSERVER::Initialize(createWebSocketResult.ExtractPayload(), messageFunc, binaryMessageFunc, nullptr, closeFunc, functionContext).release();

    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketSetBinaryMessageFragmentEventFunction(
    _In_ HCWebsocketHandle handle,
    _In_ HCWebSocketBinaryMessageFragmentFunction binaryMessageFragmentFunc
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle);
    handle->SetBinaryMessageFragmentEventFunction(binaryMessageFragmentFunc);
    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketSetProxyUri(
    _In_ HCWebsocketHandle handle,
    _In_z_ const char* proxyUri
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !proxyUri);
    return handle->websocket->SetProxyUri(proxyUri);
}
CATCH_RETURN()

STDAPI HCWebSocketSetProxyDecryptsHttps(
    _In_ HCWebsocketHandle handle,
    _In_ bool allowProxyToDecryptHttps
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle);
    return handle->websocket->SetProxyDecryptsHttps(allowProxyToDecryptHttps);
}
CATCH_RETURN()

STDAPI HCWebSocketSetHeader(
    _In_ HCWebsocketHandle handle,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !headerName || !headerValue);
    return handle->websocket->SetHeader(headerName, headerValue);
}
CATCH_RETURN()


STDAPI HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle handle,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !uri || !subProtocol);

    auto httpSingleton = get_http_singleton();
    RETURN_HR_IF(E_HC_NOT_INITIALISED, !httpSingleton);

    return httpSingleton->m_performEnv->WebSocketConnectAsyncShim(uri, subProtocol, handle, asyncBlock);
}
CATCH_RETURN()

STDAPI HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle handle,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !message);
    return handle->websocket->SendAsync(message, asyncBlock);
}
CATCH_RETURN()

STDAPI HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle handle,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !payloadBytes || !payloadSize);
    return handle->websocket->SendBinaryAsync(payloadBytes, payloadSize, asyncBlock);
}
CATCH_RETURN()

STDAPI HCWebSocketDisconnect(
    _In_ HCWebsocketHandle handle
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle);
    return handle->websocket->Disconnect();
}
CATCH_RETURN()

STDAPI HCWebSocketSetMaxReceiveBufferSize(
    _In_ HCWebsocketHandle handle,
    _In_ size_t bufferSizeInBytes
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle);
    return handle->websocket->SetMaxReceiveBufferSize(bufferSizeInBytes);
}
CATCH_RETURN()

STDAPI_(HCWebsocketHandle) HCWebSocketDuplicateHandle(
    _In_ HCWebsocketHandle handle
) noexcept
try
{
    if (handle == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketDuplicateHandle [ID %llu]", TO_ULL(handle->websocket->id));
    ++handle->refCount;

    return handle;
}
CATCH_RETURN_WITH(nullptr)

STDAPI HCWebSocketCloseHandle(
    _In_ HCWebsocketHandle handle
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle);

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCloseHandle [ID %llu]", TO_ULL(handle->websocket->id));
    int refCount = --handle->refCount;
    if (refCount <= 0)
    {
        ASSERT(refCount == 0); // should only fire at 0
        HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> reclaim{ handle };
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI HCSetWebSocketFunctions(
    _In_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_ HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc,
    _In_ HCWebSocketDisconnectFunction websocketDisconnectFunc,
    _In_opt_ void* context
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

    auto httpSingleton = get_http_singleton();
    if (httpSingleton)
    {
        return E_HC_ALREADY_INITIALISED;
    }

    auto& info = GetUserWebSocketPerformHandlers();

    info.connect = websocketConnectFunc;
    info.sendText = websocketSendMessageFunc;
    info.sendBinary = websocketSendBinaryMessageFunc;
    info.disconnect = websocketDisconnectFunc;
    info.context = context;
    return S_OK;
}
CATCH_RETURN()

STDAPI HCGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketSendBinaryMessageFunction* websocketSendBinaryMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc,
    _Out_ void** context
) noexcept
try
{
    if (websocketConnectFunc == nullptr ||
        websocketSendMessageFunc == nullptr ||
        websocketSendBinaryMessageFunc == nullptr ||
        websocketDisconnectFunc == nullptr ||
        context == nullptr)
    {
        return E_INVALIDARG;
    }

    auto const& info = GetUserWebSocketPerformHandlers();

    *websocketConnectFunc = info.connect;
    *websocketSendMessageFunc = info.sendText;
    *websocketSendBinaryMessageFunc = info.sendBinary;
    *websocketDisconnectFunc = info.disconnect;
    *context = info.context;
    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketGetProxyUri(
    _In_ HCWebsocketHandle handle,
    _Out_ const char** proxyUri
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !proxyUri);
    *proxyUri = handle->websocket->ProxyUri().data();
    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketGetHeader(
    _In_ HCWebsocketHandle handle,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !headerName || !headerValue);

    auto& headers{ handle->websocket->Headers() };
    auto it = headers.find(headerName);
    if (it != headers.end())
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

STDAPI HCWebSocketGetNumHeaders(
    _In_ HCWebsocketHandle handle,
    _Out_ uint32_t* numHeaders
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !numHeaders);
    *numHeaders = static_cast<uint32_t>(handle->websocket->Headers().size());
    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketGetHeaderAtIndex(
    _In_ HCWebsocketHandle handle,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !handle || !headerName || !headerValue);

    uint32_t index = 0;
    auto& headers{ handle->websocket->Headers() };
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
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

STDAPI HCWebSocketGetEventFunctions(
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

    if (messageFunc != nullptr)
    {
        *messageFunc = WebSocket::MessageFunc;
    }

    if (binaryMessageFunc != nullptr)
    {
        *binaryMessageFunc = WebSocket::BinaryMessageFunc;
    }

    if (closeFunc != nullptr)
    {
        *closeFunc = WebSocket::CloseFunc;
    }

    if (context != nullptr)
    {
        *context = nullptr;
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketGetBinaryMessageFragmentEventFunction(
    _In_ HCWebsocketHandle websocket,
    _Out_ HCWebSocketBinaryMessageFragmentFunction* binaryMessageFragmentFunc,
    _Out_ void** context
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !websocket || !binaryMessageFragmentFunc || !context);

    *binaryMessageFragmentFunc = WebSocket::BinaryMessageFragmentFunc;
    *context = nullptr;
    return S_OK;
}
CATCH_RETURN()


STDAPI HCGetWebSocketConnectResult(
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

STDAPI HCGetWebSocketSendMessageResult(
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

#endif //!HC_NOWEBSOCKETS
