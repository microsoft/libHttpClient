// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include <atomic>
#include <assert.h>
#include <vector>

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

struct WebSocketContext
{
    WebSocketContext()
    {
        closeEventHandle = CreateEvent(nullptr, false, false, nullptr);
    }

    ~WebSocketContext()
    {
        if (handle)
        {
            HCWebSocketCloseHandle(handle);
        }
        CloseHandle(closeEventHandle);
    }

    HCWebsocketHandle handle{ nullptr };
    std::vector<char> receiveBuffer;
    std::atomic<uint32_t> messagesReceived;
    HANDLE closeEventHandle;
};

void CALLBACK WebSocketMessageReceived(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* incomingBodyString,
    _In_opt_ void* functionContext
)
{
    auto ctx = static_cast<WebSocketContext*>(functionContext);

    printf_s("Received websocket message: %s\n", incomingBodyString);
    ++ctx->messagesReceived;
}

void CALLBACK WebSocketBinaryMessageReceived(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _In_ void* functionContext
)
{
    auto ctx = static_cast<WebSocketContext*>(functionContext);

    printf_s("Received websocket binary message of size: %u\r\n", payloadSize);

    ++ctx->messagesReceived;
}

void CALLBACK WebSocketBinaryMessageFragmentReceived(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _In_ bool isFinalFragment,
    _In_ void* functionContext
)
{
    auto ctx = static_cast<WebSocketContext*>(functionContext);

    printf("Received websocket binary message fragment of size %u\r\n", payloadSize);
    ctx->receiveBuffer.insert(ctx->receiveBuffer.end(), payloadBytes, payloadBytes + payloadSize);
    if (isFinalFragment)
    {
        printf_s("Full message now received: %s\n", ctx->receiveBuffer.data());
        ++ctx->messagesReceived;
        ctx->receiveBuffer.clear();
    }
}

void CALLBACK WebSocketClosed(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* functionContext
    )
{
    auto ctx = static_cast<WebSocketContext*>(functionContext);

    printf_s("Websocket closed!\n");
    SetEvent(ctx->closeEventHandle);
}

int main()
{
    HCInitialize(nullptr);
    HCSettingsSetTraceLevel(HCTraceLevel::Verbose);

    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &g_queue);
    XTaskQueueRegisterMonitor(g_queue, nullptr, HandleAsyncQueueCallback, &g_callbackToken);
    StartBackgroundThread();

    std::string url = "ws://localhost:9002";

    auto websocketContext = new WebSocketContext{};
    HCWebSocketCreate(&websocketContext->handle, WebSocketMessageReceived, WebSocketBinaryMessageReceived, WebSocketClosed, websocketContext);
    HCWebSocketSetBinaryMessageFragmentEventFunction(websocketContext->handle, WebSocketBinaryMessageFragmentReceived);
    HCWebSocketSetMaxReceiveBufferSize(websocketContext->handle, 4096);

    XAsyncBlock* asyncBlock = new XAsyncBlock{};
    asyncBlock->queue = g_queue;
    asyncBlock->callback = [](XAsyncBlock* asyncBlock)
    {
        WebSocketCompletionResult result = {};
        HRESULT hr = HCGetWebSocketConnectResult(asyncBlock, &result);
        assert(SUCCEEDED(hr));

        if (SUCCEEDED(hr))
        {
            printf_s("HCWebSocketConnect complete: %d, %d\n", result.errorCode, result.platformErrorCode);
            if (FAILED(result.errorCode))
            {
                throw std::exception("Connect failed. Make sure a local echo server is running.");
            }
        }

        delete asyncBlock;
    };

    printf_s("Calling HCWebSocketConnect...\n");
    HCWebSocketConnectAsync(url.data(), "", websocketContext->handle, asyncBlock);
    XAsyncGetStatus(asyncBlock, true);

    uint32_t sendLoops = 2;
    for (uint32_t i = 1; i <= sendLoops; i++)
    {
        // Test with message just larger than the configured receive buffer size
        char webMsg[4100];
        for (uint32_t j = 0; j < sizeof(webMsg); ++j)
        {
            webMsg[j] = 'X';
        }
        webMsg[sizeof(webMsg) - 1] = '\0';

        asyncBlock = new XAsyncBlock{};
        asyncBlock->queue = g_queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
        {
            WebSocketCompletionResult result = {};
            HRESULT hr = HCGetWebSocketSendMessageResult(asyncBlock, &result);
            assert(SUCCEEDED(hr));

            if (SUCCEEDED(hr))
            {
                printf_s("HCWebSocketSendMessage complete: %d, %d\n", result.errorCode, result.platformErrorCode);
            }
            delete asyncBlock;
        };

        printf_s("Calling HCWebSocketSend with message \"%s\" and waiting for response...\n", webMsg);
        HCWebSocketSendMessageAsync(websocketContext->handle, webMsg, asyncBlock);

        asyncBlock = new XAsyncBlock{};
        asyncBlock->queue = g_queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
        {
            WebSocketCompletionResult result = {};
            HRESULT hr = HCGetWebSocketSendMessageResult(asyncBlock, &result);
            assert(SUCCEEDED(hr));

            if (SUCCEEDED(hr))
            {
                printf_s("HCWebSocketSendBinaryMessageAsync complete: %d, %d\n", result.errorCode, result.platformErrorCode);
            }

            delete asyncBlock;
        };

        HCWebSocketSendBinaryMessageAsync(websocketContext->handle, (uint8_t*)webMsg, 100, asyncBlock);
    }

    while (websocketContext->messagesReceived < sendLoops * 2) // Two messages sent each loop iteration
    {
        Sleep(10);
    }

    printf_s("Calling HCWebSocketDisconnect...\n");
    HCWebSocketDisconnect(websocketContext->handle);
    WaitForSingleObject(websocketContext->closeEventHandle, INFINITE);
    delete websocketContext;

    HCCleanup();

    return 0;
}

