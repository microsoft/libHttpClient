// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "stdafx.h"
#include "httpClient\httpClient.h"

std::vector<std::vector<std::wstring>> ExtractAllHeaders(_In_ HC_CALL_HANDLE call)
{
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);

    std::vector< std::vector<std::wstring> > headers;
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const WCHAR* str;
        const WCHAR* str2;
        std::wstring headerName;
        std::wstring headerValue;
        HCHttpCallResponseGetHeaderAtIndex(call, i, &str, &str2);
        if (str != nullptr) headerName = str;
        if (str2 != nullptr) headerValue = str2;
        std::vector<std::wstring> header;
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
DWORD g_targetNumThreads = 2;
HANDLE g_hActiveThreads[10] = { 0 };
DWORD g_defaultIdealProcessor = 0;
DWORD g_numActiveThreads = 0;

DWORD WINAPI http_thread_proc(LPVOID lpParam)
{
    HANDLE hEvents[2] =
    {
        HCTaskGetPendingHandle(),
        g_stopRequestedHandle.get()
    };

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(2, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // pending ready
            HCTaskProcessNextPendingTask();
            break;

        default:
            stop = true;
            break;
        }
    }

    return 0;
}

void InitBackgroundThread()
{
    for (uint32_t i = 0; i < g_targetNumThreads; i++)
    {
        g_hActiveThreads[i] = CreateThread(nullptr, 0, http_thread_proc, nullptr, 0, nullptr);
        if (g_defaultIdealProcessor != MAXIMUM_PROCESSORS)
        {
            SetThreadIdealProcessor(g_hActiveThreads[i], g_defaultIdealProcessor);
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
    std::wstring method = L"GET";
    std::wstring url = L"http://www.bing.com";
    std::wstring requestBody = L"";
    bool retryAllowed = true;
    std::vector<std::vector<std::wstring>> headers;
    std::vector< std::wstring > header;

    header.clear();
    header.push_back(L"User-Agent");
    header.push_back(L"libHttpClient");
    headers.push_back(header);

    HCGlobalInitialize();

    InitBackgroundThread();

    HC_CALL_HANDLE call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, method.c_str(), url.c_str());
    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, retryAllowed);
    for (auto& header : headers)
    {
        std::wstring headerName = header[0];
        std::wstring headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str());
    }

    wprintf_s(L"Calling %s %s\r\n", method.c_str(), url.c_str());

    uint64_t taskGroupId = 0;
    auto taskHandle = HCHttpCallPerform(taskGroupId, call, nullptr,
        [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            const WCHAR* str;
            uint32_t errCode = 0;
            uint32_t statusCode = 0;
            std::wstring responseString;
            std::wstring errMessage;

            HCHttpCallResponseGetErrorCode(call, &errCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &str);
            if (str != nullptr) responseString = str;
            HCHttpCallResponseGetErrorMessage(call, &str);
            if (str != nullptr) errMessage = str;
            std::vector<std::vector<std::wstring>> headers = ExtractAllHeaders(call);

            HCHttpCallCleanup(call);

            wprintf_s(L"Got ErrorCode:%d HttpStatus:%d\r\n", errCode, statusCode);
            wprintf_s(L"responseString:%s\r\n", responseString.c_str());
        });

    HCTaskWaitForCompleted(taskHandle, 1000*1000);
    HCTaskProcessNextCompletedTask(0);
    HCGlobalCleanup();

    return 0;
}

