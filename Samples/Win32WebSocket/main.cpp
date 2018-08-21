// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

HANDLE g_eventHandle;

void message_received(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ const char* incomingBodyString
    )
{
    printf_s("Recieved websocket message: %s\n", incomingBodyString);
    SetEvent(g_eventHandle);
}

void websocket_closed(
    _In_ hc_websocket_handle_t websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    )
{
    printf_s("Websocket closed!\n");
    SetEvent(g_eventHandle);
}

int main()
{
    g_eventHandle = CreateEvent(nullptr, false, false, nullptr);

    HCInitialize(nullptr);
    HCSettingsSetTraceLevel(HCTraceLevel_Verbose);

    async_queue_handle_t queue;
    uint32_t sharedAsyncQueueId = 0;
    CreateSharedAsyncQueue(
        sharedAsyncQueueId,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_ThreadPool,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_ThreadPool,
        &queue);

    std::string url = "wss://echo.websocket.org";

    hc_websocket_handle_t websocket;
    HRESULT hr = HCWebSocketCreate(&websocket);
    hr = HCWebSocketSetFunctions(message_received, websocket_closed);

    AsyncBlock* asyncBlock = new AsyncBlock{};
    asyncBlock->queue = queue;
    asyncBlock->callback = [](AsyncBlock* asyncBlock)
    {
        WebSocketCompletionResult result = {};
        HCGetWebSocketConnectResult(asyncBlock, &result);

        printf_s("HCWebSocketConnect complete: %d, %d\n", result.errorCode, result.platformErrorCode);
        SetEvent(g_eventHandle);
        delete asyncBlock;
    };

    printf_s("Calling HCWebSocketConnect...\n");
    hr = HCWebSocketConnectAsync(url.data(), "", websocket, asyncBlock);
    WaitForSingleObject(g_eventHandle, INFINITE);

    asyncBlock = new AsyncBlock{};
    asyncBlock->queue = queue;
    asyncBlock->callback = [](AsyncBlock* asyncBlock)
    {
        WebSocketCompletionResult result = {};
        HCGetWebSocketSendMessageResult(asyncBlock, &result);

        printf_s("HCWebSocketSendMessage complete: %d, %d\n", result.errorCode, result.platformErrorCode);
        SetEvent(g_eventHandle);
        delete asyncBlock;
    };

    std::string requestString = "This message should be echoed!";
    printf_s("Calling HCWebSocketSend with message \"%s\" and waiting for response...\n", requestString.data());
    hr = HCWebSocketSendMessageAsync(websocket, requestString.data(), asyncBlock);
    
    // Wait for send to complete sucessfully and then wait again for response to be received.
    WaitForSingleObject(g_eventHandle, INFINITE);
    WaitForSingleObject(g_eventHandle, INFINITE);

    printf_s("Calling HCWebSocketDisconnect...\n");
    HCWebSocketDisconnect(websocket);
    WaitForSingleObject(g_eventHandle, INFINITE);

    HCWebSocketCloseHandle(websocket);
    CloseHandle(g_eventHandle);
    return 0;
}

