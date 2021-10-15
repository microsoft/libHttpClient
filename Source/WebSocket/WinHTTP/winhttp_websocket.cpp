// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#if HC_WINHTTP_WEBSOCKETS

#include "winhttp_websocket.h"
#if HC_PLATFORM == HC_PLATFORM_GDK
#include "../../HTTP/Curl/CurlProvider.h"
#else
#include "../../HTTP/WinHttp/winhttp_provider.h"
#endif

using namespace xbox::httpclient;

HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv env
    )
{
    RETURN_HR_IF(E_UNEXPECTED, !env);
    return env->winHttpProvider->WebSocketConnect(uri, subProtocol, websocket, asyncBlock);
}

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* async,
    _In_opt_ void* /*context*/
    )
{
    RETURN_HR_IF(E_INVALIDARG, !websocket);

    auto socketImpl = std::dynamic_pointer_cast<WinHttpWebSocket>(websocket->impl);
    RETURN_HR_IF(E_UNEXPECTED, !socketImpl);

    return socketImpl->winHttpConnection->WebSocketSendMessageAsync(async, message);
}

HRESULT CALLBACK Internal_HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/)
{
    RETURN_HR_IF(E_INVALIDARG, !websocket);

    auto socketImpl = std::dynamic_pointer_cast<WinHttpWebSocket>(websocket->impl);
    RETURN_HR_IF(E_UNEXPECTED, !socketImpl);

    return socketImpl->winHttpConnection->WebSocketSendMessageAsync(asyncBlock, payloadBytes, payloadSize);
}

HRESULT CALLBACK Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* /*context*/
    )
{
    RETURN_HR_IF(E_INVALIDARG, !websocket);

    auto socketImpl = std::dynamic_pointer_cast<WinHttpWebSocket>(websocket->impl);
    RETURN_HR_IF(E_UNEXPECTED, !socketImpl);

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", TO_ULL(websocket->id));

    return socketImpl->winHttpConnection->WebSocketDisconnect(closeStatus);
}

#endif
