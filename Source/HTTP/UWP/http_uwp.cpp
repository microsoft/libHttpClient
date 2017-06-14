// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if UWP_API
#include "httpClient/types.h"
#include "httpClient/httpClient.h"
#include "singleton.h"
#include "asyncop.h"

using namespace Windows::Foundation;
using namespace Windows::Web::Http;

void OnDone(IAsyncOperation<Platform::String^>^ asyncOp, AsyncStatus s)
{
}

class Win32Event
{
public:
    Win32Event()
    {
        m_event = CreateEvent(NULL, TRUE, FALSE, nullptr);
    }

    ~Win32Event()
    {
        CloseHandle(m_event);
    }

    void Set()
    {
        SetEvent(m_event);
    }

    void WaitForever()
    {
        WaitForSingleObject(m_event, INFINITE);
    }

private:
    HANDLE m_event;
};

template<typename T1, typename T2>
class HttpTaskWithProgress : public std::enable_shared_from_this< HttpTaskWithProgress<T1,T2> >
{
public:
    IAsyncOperationWithProgress<T1, T2>^ m_asyncOp;
    AsyncStatus m_status;
    Win32Event m_event;
    T1 m_result;

    static std::shared_ptr<HttpTaskWithProgress<T1,T2>> New() 
    {
        return std::make_shared<HttpTaskWithProgress<T1, T2>>();
        //return std::allocate_shared< HttpTaskWithProgress<T1, T2>, http_stl_allocator<HttpTaskWithProgress<T1, T2> >();
    }

    HttpTaskWithProgress()
    {
    }

    void Init(IAsyncOperationWithProgress<T1, T2>^ asyncOp)
    {
        m_status = AsyncStatus::Started;
        m_asyncOp = asyncOp;
    }

    void WaitForever()
    {   
        std::weak_ptr< HttpTaskWithProgress<T1, T2> > thisWeakPtr = shared_from_this();

        m_asyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<T1, T2>(
            [thisWeakPtr](IAsyncOperationWithProgress<T1, T2>^ asyncOp, AsyncStatus status)
        {
            std::shared_ptr< HttpTaskWithProgress<T1, T2> > pThis(thisWeakPtr.lock());
            if (pThis)
            {
                pThis->m_status = status;
                pThis->m_result = asyncOp->GetResults();
                pThis->m_event.Set();
            }
        });

        m_event.WaitForever();
    }

    T1 GetResult()
    {
        return m_result;
    }

};

void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call
    )
{
    const WCHAR* url = nullptr;
    const WCHAR* method = nullptr;
    const WCHAR* requestBody = nullptr;
    const WCHAR* userAgent = nullptr;
    HCHttpCallRequestGetUrl(call, &method, &url, &requestBody);
    HCHttpCallRequestGetHeader(call, L"User-Agent", &userAgent);

    try
    {
        HttpClient^ httpClient = ref new HttpClient();
        auto headers = httpClient->DefaultRequestHeaders;
        Uri^ requestUri = ref new Uri(ref new Platform::String(url));

        auto taskHttpClientGet = HttpTaskWithProgress<Windows::Web::Http::HttpResponseMessage^, Windows::Web::Http::HttpProgress>::New();
        taskHttpClientGet->Init(httpClient->GetAsync(requestUri));
        taskHttpClientGet->WaitForever();
        HttpResponseMessage^ httpResponse = taskHttpClientGet->GetResult();

        auto taskReadAsString = HttpTaskWithProgress<Platform::String^, unsigned long long>::New();
        taskReadAsString->Init(httpResponse->Content->ReadAsStringAsync());
        taskReadAsString->WaitForever();
        Platform::String^ httpResponseBody = taskReadAsString->GetResult();

        HCHttpCallResponseSetResponseString(call, httpResponseBody->Data());
    }
    catch (Platform::Exception^ ex)
    {
        HCHttpCallResponseSetErrorCode(call, ex->HResult);
    }
}

#endif