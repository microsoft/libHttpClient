// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include <atomic>

class win32_handle
{
public:
    win32_handle() : m_handle(nullptr)
    {
    }

    ~win32_handle()
    {
        if (m_handle != nullptr) CloseHandle(m_handle);
        m_handle = nullptr;
    }

    void set(HANDLE handle)
    {
        m_handle = handle;
    }

    HANDLE get() { return m_handle; }

private:
    HANDLE m_handle;
};

win32_handle g_stopRequestedHandle;
win32_handle g_workReadyHandle;
win32_handle g_completionReadyHandle;
win32_handle g_exampleTaskDone;

DWORD g_targetNumThreads = 2;
HANDLE g_hActiveThreads[10] = { 0 };
DWORD g_defaultIdealProcessor = 0;
DWORD g_numActiveThreads = 0;

XTaskQueueHandle g_queue;
XTaskQueueRegistrationToken g_callbackToken;

DWORD WINAPI background_thread_proc(LPVOID lpParam)
{
    HANDLE hEvents[3] =
    {
        g_workReadyHandle.get(),
        g_completionReadyHandle.get(),
        g_stopRequestedHandle.get()
    };

    XTaskQueueHandle queue;
    XTaskQueueDuplicateHandle(g_queue, &queue);
    HCTraceSetTraceToDebugger(true);
    HCSettingsSetTraceLevel(HCTraceLevel::Verbose);

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(3, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // work ready
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0))
            {
                // If we executed work, set our event again to check next time.
                SetEvent(g_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            // Typically completions should be dispatched on the game thread, but
            // for this simple XAML app we're doing it here
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
            {
                // If we executed a completion set our event again to check next time
                SetEvent(g_completionReadyHandle.get());
            }
            break;

        default:
            stop = true;
            break;
        }
    }

    XTaskQueueCloseHandle(queue);
    return 0;
}

void CALLBACK HandleAsyncQueueCallback(
    _In_ void* context,
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort type
)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(queue);

    switch (type)
    {
    case XTaskQueuePort::Work:
        SetEvent(g_workReadyHandle.get());
        break;

    case XTaskQueuePort::Completion:
        SetEvent(g_completionReadyHandle.get());
        break;
    }
}

void StartBackgroundThread()
{
    g_stopRequestedHandle.set(CreateEvent(nullptr, true, false, nullptr));
    g_workReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    g_completionReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    g_exampleTaskDone.set(CreateEvent(nullptr, false, false, nullptr));

    for (uint32_t i = 0; i < g_targetNumThreads; i++)
    {
        g_hActiveThreads[i] = CreateThread(nullptr, 0, background_thread_proc, nullptr, 0, nullptr);
        if (g_defaultIdealProcessor != MAXIMUM_PROCESSORS)
        {
            if (g_hActiveThreads[i] != nullptr)
            {
                SetThreadIdealProcessor(g_hActiveThreads[i], g_defaultIdealProcessor);
            }
        }
    }

    g_numActiveThreads = g_targetNumThreads;
}

void ShutdownActiveThreads()
{
    SetEvent(g_stopRequestedHandle.get());
    DWORD dwResult = WaitForMultipleObjectsEx(g_numActiveThreads, g_hActiveThreads, true, INFINITE, false);
    if (dwResult >= WAIT_OBJECT_0 && dwResult <= WAIT_OBJECT_0 + g_numActiveThreads - 1)
    {
        for (DWORD i = 0; i < g_numActiveThreads; i++)
        {
            CloseHandle(g_hActiveThreads[i]);
            g_hActiveThreads[i] = nullptr;
        }
        g_numActiveThreads = 0;
        ResetEvent(g_stopRequestedHandle.get());
    }
}

HANDLE g_eventHandle;
std::atomic<uint32_t> g_numberMessagesReceieved = 0;

void message_received(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* incomingBodyString,
    _In_opt_ void* functionContext
    )
{
    printf_s("Received websocket message: %s\n", incomingBodyString);
    g_numberMessagesReceieved++;
    SetEvent(g_eventHandle);
}

void websocket_closed(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* functionContext
    )
{
    printf_s("Websocket closed!\n");
    SetEvent(g_eventHandle);
}

int main()
{
    g_eventHandle = CreateEvent(nullptr, false, false, nullptr);

    HCInitialize(nullptr);
    HCSettingsSetTraceLevel(HCTraceLevel::Verbose);

    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &g_queue);
    XTaskQueueRegisterMonitor(g_queue, nullptr, HandleAsyncQueueCallback, &g_callbackToken);
    StartBackgroundThread();

    std::string url = "wss://echo.websocket.org";

    HCWebsocketHandle websocket;

    HRESULT hr = HCWebSocketCreate(&websocket, message_received, nullptr, websocket_closed, nullptr);

    for (int iConnectAttempt = 0; iConnectAttempt < 10; iConnectAttempt++)
    {
        g_numberMessagesReceieved = 0;

        XAsyncBlock* asyncBlock = new XAsyncBlock{};
        asyncBlock->queue = g_queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
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
        
        uint32_t numberOfMessagesToSend = 100;
        for (uint32_t i = 1; i <= numberOfMessagesToSend; i++)
        {
            asyncBlock = new XAsyncBlock{};
            asyncBlock->queue = g_queue;
            asyncBlock->callback = [](XAsyncBlock* asyncBlock)
            {
                WebSocketCompletionResult result = {};
                HCGetWebSocketSendMessageResult(asyncBlock, &result);

                printf_s("HCWebSocketSendMessage complete: %d, %d\n", result.errorCode, result.platformErrorCode);
                SetEvent(g_eventHandle);
                delete asyncBlock;
            };

            char webMsg[100];
            snprintf(webMsg, sizeof(webMsg), "Message #%d should be echoed!", i);
            printf_s("Calling HCWebSocketSend with message \"%s\" and waiting for response...\n", webMsg);
            hr = HCWebSocketSendMessageAsync(websocket, webMsg, asyncBlock);
        }

        while (g_numberMessagesReceieved < numberOfMessagesToSend)
        {
            WaitForSingleObject(g_eventHandle, INFINITE);
        }

        printf_s("Calling HCWebSocketDisconnect...\n");
        HCWebSocketDisconnect(websocket);
        WaitForSingleObject(g_eventHandle, INFINITE);
    }

    HCWebSocketCloseHandle(websocket);
    CloseHandle(g_eventHandle);
    return 0;
}

