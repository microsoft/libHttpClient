// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if UWP_API
#include "httpClient/types.h"
#include "httpClient/httpClient.h"
#include "singleton.h"
#include "asyncop.h"
#include "../httpcall.h"
#include "UWP/utils_uwp.h"

using namespace Windows::Foundation;
using namespace Windows::Web::Http;


class uwp_http_task : public hc_task
{
private:
    IAsyncOperationWithProgress<HttpResponseMessage^, HttpProgress>^ m_getHttpAsyncOp;
    AsyncStatus m_getHttpAsyncOpStatus;

    IAsyncOperationWithProgress<Platform::String^, unsigned long long>^ m_readAsStringAsyncOp;
    AsyncStatus m_readAsStringStatus;

public:
    uwp_http_task() : 
        m_getHttpAsyncOpStatus(AsyncStatus::Started),
        m_readAsStringStatus(AsyncStatus::Started)
    {
    }

    void perform_async(
        _In_ HC_CALL_HANDLE call,
        _In_ HC_ASYNC_TASK_HANDLE taskHandle
        );
};


void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_ASYNC_TASK_HANDLE taskHandle
    )
{
    std::shared_ptr<uwp_http_task> uwpHttpTask = std::make_shared<uwp_http_task>();
    call->task = std::dynamic_pointer_cast<hc_task>(uwpHttpTask);

    uwpHttpTask->perform_async(call, taskHandle);
}


void uwp_http_task::perform_async(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_ASYNC_TASK_HANDLE taskHandle
    )
{
    try
    {
        const WCHAR* url = nullptr;
        const WCHAR* method = nullptr;
        const WCHAR* requestBody = nullptr;
        const WCHAR* userAgent = nullptr;
        HCHttpCallRequestGetUrl(call, &method, &url);
        HCHttpCallRequestGetRequestBodyString(call, &requestBody);
        HCHttpCallRequestGetHeader(call, L"User-Agent", &userAgent);

        HttpClient^ httpClient = ref new HttpClient();
        auto headers = httpClient->DefaultRequestHeaders;
        Uri^ requestUri = ref new Uri(ref new Platform::String(url));

        m_getHttpAsyncOp = httpClient->GetAsync(requestUri);
        m_getHttpAsyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<HttpResponseMessage^, HttpProgress>(
            [call, taskHandle](IAsyncOperationWithProgress<HttpResponseMessage^, HttpProgress>^ asyncOp, AsyncStatus status)
        {
            try
            {
                std::shared_ptr<uwp_http_task> uwpHttpTask = std::dynamic_pointer_cast<uwp_http_task>(call->task);
                uwpHttpTask->m_getHttpAsyncOpStatus = status;
                HttpResponseMessage^ httpResponse = asyncOp->GetResults();

                uwpHttpTask->m_readAsStringAsyncOp = httpResponse->Content->ReadAsStringAsync();
                uwpHttpTask->m_readAsStringAsyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<Platform::String^, unsigned long long>(
                    [call, taskHandle](IAsyncOperationWithProgress<Platform::String^, unsigned long long>^ asyncOp, AsyncStatus status)
                {
                    try
                    {
                        Platform::String^ httpResponseBody = asyncOp->GetResults();
						HCHttpCallResponseSetResponseString(call, httpResponseBody->Data());
                        HCThreadSetResultsReady(taskHandle);
                    }
                    catch (Platform::Exception^ ex)
                    {
						HCHttpCallResponseSetErrorCode(call, ex->HResult);
                        HCThreadSetResultsReady(taskHandle);
                    }
                });
            }
            catch (Platform::Exception^ ex)
            {
                HCHttpCallResponseSetErrorCode(call, ex->HResult);
				HCThreadSetResultsReady(taskHandle);
            }
        });
    }
    catch (Platform::Exception^ ex)
    {
		HCHttpCallResponseSetErrorCode(call, ex->HResult);
		HCThreadSetResultsReady(taskHandle);
    }
}

#endif