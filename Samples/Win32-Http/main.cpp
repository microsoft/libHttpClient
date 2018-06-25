// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "stdafx.h"
#include "httpClient\httpClient.h"
#include "json_cpp\json.h"

std::vector<std::vector<std::string>> ExtractAllHeaders(_In_ hc_call_handle_t call)
{
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);

    std::vector< std::vector<std::string> > headers;
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* str;
        const char* str2;
        std::string headerName;
        std::string headerValue;
        HCHttpCallResponseGetHeaderAtIndex(call, i, &str, &str2);
        if (str != nullptr) headerName = str;
        if (str2 != nullptr) headerValue = str2;
        std::vector<std::string> header;
        header.push_back(headerName);
        header.push_back(headerValue);

        headers.push_back(header);
    }

    return headers;
}

class win32_handle
{
public:
    win32_handle() : m_handle(nullptr)
    {
    }

    ~win32_handle()
    {
        if( m_handle != nullptr ) CloseHandle(m_handle);
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

async_queue_handle_t g_queue;
uint32_t g_callbackToken;

DWORD WINAPI background_thread_proc(LPVOID lpParam)
{
    HANDLE hEvents[3] =
    {
        g_workReadyHandle.get(),
        g_completionReadyHandle.get(),
        g_stopRequestedHandle.get()
    };

    async_queue_handle_t queue;
    uint32_t sharedAsyncQueueId = 0;
    CreateSharedAsyncQueue(
        sharedAsyncQueueId,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        &queue);

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(3, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // work ready
            DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 0);

            if (!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work))
            {
                // If there's more pending work, then set the event to process them
                SetEvent(g_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            // Typically completions should be dispatched on the game thread, but
            // for this simple XAML app we're doing it here
            DispatchAsyncQueue(queue, AsyncQueueCallbackType_Completion, 0);

            if (!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion))
            {
                // If there's more pending completions, then set the event to process them
                SetEvent(g_completionReadyHandle.get());
            }
            break;

        default:
            stop = true;
            break;
        }
    }

    return 0;
}

void HandleAsyncQueueCallback(
    _In_ void* context,
    _In_ async_queue_handle_t queue,
    _In_ AsyncQueueCallbackType type
    )
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(queue);

    switch (type)
    {
    case AsyncQueueCallbackType::AsyncQueueCallbackType_Work:
        SetEvent(g_workReadyHandle.get());
        break;

    case AsyncQueueCallbackType::AsyncQueueCallbackType_Completion:
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

int main()
{
    std::string method = "GET";
    std::string url = "https://www.bing.com/?rb=0";
    std::string requestBody = "";// "{\"test\":\"value\"},{\"test2\":\"value\"},{\"test3\":\"value\"},{\"test4\":\"value\"},{\"test5\":\"value\"},{\"test6\":\"value\"},{\"test7\":\"value\"}";
    bool retryAllowed = true;
    std::vector<std::vector<std::string>> headers;
    std::vector< std::string > header;

    header.clear();
    header.push_back("TestHeader");
    header.push_back("1.0");
    headers.push_back(header);

    HCInitialize(nullptr);

    uint32_t sharedAsyncQueueId = 0;
    CreateSharedAsyncQueue(
        sharedAsyncQueueId,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        &g_queue);
    AddAsyncQueueCallbackSubmitted(g_queue, nullptr, HandleAsyncQueueCallback, &g_callbackToken);

    StartBackgroundThread();

    hc_call_handle_t call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, method.c_str(), url.c_str());
    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, retryAllowed);
    for (auto& header : headers)
    {
        std::string headerName = header[0];
        std::string headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str(), true);
    }

    printf_s("Calling %s %s\r\n", method.c_str(), url.c_str());

    AsyncBlock* asyncBlock = new AsyncBlock;
    ZeroMemory(asyncBlock, sizeof(AsyncBlock));
    asyncBlock->context = call;
    asyncBlock->queue = g_queue;
    asyncBlock->callback = [](AsyncBlock* asyncBlock)
    {
        const char* str;
        HRESULT errCode = S_OK;
        uint32_t platErrCode = 0;
        uint32_t statusCode = 0;
        std::string responseString;
        std::string errMessage;

        hc_call_handle_t call = static_cast<hc_call_handle_t>(asyncBlock->context);
        HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
        HCHttpCallResponseGetStatusCode(call, &statusCode);
        HCHttpCallResponseGetResponseString(call, &str);
        if (str != nullptr) responseString = str;
        std::vector<std::vector<std::string>> headers = ExtractAllHeaders(call);

        // Uncomment to write binary file to disk
        //size_t bufferSize = 0;
        //HCHttpCallResponseGetResponseBodyBytesSize(call, &bufferSize);
        //uint8_t* buffer = new uint8_t[bufferSize];
        //size_t bufferUsed = 0;
        //HCHttpCallResponseGetResponseBodyBytes(call, bufferSize, buffer, &bufferUsed);
        //HANDLE hFile = CreateFile(L"c:\\test\\test.zip", GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        //DWORD bufferWritten = 0;
        //WriteFile(hFile, buffer, (DWORD)bufferUsed, &bufferWritten, NULL);
        //CloseHandle(hFile);
        //delete[] buffer;

        HCHttpCallCloseHandle(call);

        printf_s("HTTP call done\r\n");
        printf_s("Network error code: %d\r\n", errCode);
        printf_s("HTTP status code: %d\r\n", statusCode);

        int i = 0;
        for (auto& header : headers)
        {
            printf_s("Header[%d] '%s'='%s'\r\n", i, header[0].c_str(), header[1].c_str());
            i++;
        }

        if (responseString.length() > 200)
        {
            std::string subResponseString = responseString.substr(0, 200);
            printf_s("Response string:\r\n%s...\r\n", subResponseString.c_str());
        }
        else
        {
            printf_s("Response string:\r\n%s\r\n", responseString.c_str());
        }

        SetEvent(g_exampleTaskDone.get());
        delete asyncBlock;
    };

    HCHttpCallPerformAsync(asyncBlock, call);

    WaitForSingleObject(g_exampleTaskDone.get(), INFINITE);

    ShutdownActiveThreads();
    HCCleanup();

    return 0;
}

