// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#if HC_UWP_API
#include <robuffer.h>
#include <wrl.h>

#include "../httpcall.h"
#include "win/utils_win.h"

using namespace Windows::Foundation;
using namespace Windows::Web::Http;


class uwp_http_task : public xbox::httpclient::hc_task
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
        _In_ hc_call_handle_t call,
        _In_ AsyncBlock* asyncBlock
        );
};


void Internal_HCHttpCallPerform(
    _In_ hc_call_handle_t call,
    _In_ AsyncBlock* asyncBlock
    )
{
    uwp_http_task* uwpHttpTask = new uwp_http_task();
    HCHttpCallSetContext(call, uwpHttpTask);
    uwpHttpTask->perform_async(call, asyncBlock);
}


void uwp_http_task::perform_async(
    _In_ hc_call_handle_t call,
    _In_ AsyncBlock* asyncBlock
    )
{
    try
    {
        const char* url = nullptr;
        const char* method = nullptr;
        const uint8_t* requestBody = nullptr;
        uint32_t requestBodySize = 0;
        const char* userAgent = nullptr;
        HCHttpCallRequestGetUrl(call, &method, &url);
        HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodySize);

        uint32_t numHeaders = 0;
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);

        HttpClient^ httpClient = ref new HttpClient();
        http_internal_wstring wUrl = utf16_from_utf8(url);
        http_internal_wstring wMethod = utf16_from_utf8(method);
        Uri^ requestUri = ref new Uri(ref new Platform::String(wUrl.c_str()));
        HttpRequestMessage^ requestMsg = ref new HttpRequestMessage(ref new HttpMethod(ref new Platform::String(wMethod.c_str())), requestUri);

        requestMsg->Headers->TryAppendWithoutValidation(L"User-Agent", L"libHttpClient/1.0.0.0");

        for (uint32_t i = 0; i < numHeaders; i++)
        {
            const char* headerName;
            const char* headerValue;
            HCHttpCallRequestGetHeaderAtIndex(call, i, &headerName, &headerValue);
            if (headerName != nullptr && headerValue != nullptr)
            {
                http_internal_wstring wHeaderName = utf16_from_utf8(headerName);
                http_internal_wstring wHeaderValue = utf16_from_utf8(headerValue);
                requestMsg->Headers->TryAppendWithoutValidation(
                    ref new Platform::String(wHeaderName.c_str()),
                    ref new Platform::String(wHeaderValue.c_str()
                    ));
            }
        }

        requestMsg->Headers->AcceptEncoding->TryParseAdd(Platform::StringReference(L"gzip"));
        requestMsg->Headers->AcceptEncoding->TryParseAdd(Platform::StringReference(L"deflate"));
        requestMsg->Headers->AcceptEncoding->TryParseAdd(Platform::StringReference(L"br"));
        requestMsg->Headers->Accept->TryParseAdd(Platform::StringReference(L"*/*"));

        if (requestBody != nullptr)
        {
            // create an IBuffer
            auto buffer = ref new Windows::Storage::Streams::Buffer(requestBodySize);
            buffer->Length = requestBodySize; // set the length

            // Get hold of the IBufferByteAccess interface that actually lets us see the data
            Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;
            reinterpret_cast<IInspectable*>(buffer)->QueryInterface(IID_PPV_ARGS(&bufferByteAccess));

            // Get pointer to the data and copy
            uint8_t* bufferMemory = nullptr;
            bufferByteAccess->Buffer(&bufferMemory);
            memcpy(bufferMemory, requestBody, requestBodySize);

            requestMsg->Content = ref new HttpBufferContent(buffer);
            requestMsg->Content->Headers->ContentType = Windows::Web::Http::Headers::HttpMediaTypeHeaderValue::Parse(L"application/json; charset=utf-8");
        }

        m_getHttpAsyncOp = httpClient->SendRequestAsync(requestMsg, HttpCompletionOption::ResponseContentRead);
        m_getHttpAsyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<HttpResponseMessage^, HttpProgress>(
            [call, asyncBlock](IAsyncOperationWithProgress<HttpResponseMessage^, HttpProgress>^ asyncOp, AsyncStatus status)
        {
            try
            {
                void* callContext = nullptr;
                HCHttpCallGetContext(call, &callContext);
                uwp_http_task* uwpHttpTask = static_cast<uwp_http_task*>(callContext); // TODO: cleanup at close
                if (uwpHttpTask != nullptr)
                {
                    uwpHttpTask->m_getHttpAsyncOpStatus = status;
                    HttpResponseMessage^ httpResponse = asyncOp->GetResults();

                    uint32_t statusCode = (uint32_t)httpResponse->StatusCode;
                    HCHttpCallResponseSetStatusCode(call, statusCode);

                    auto view = httpResponse->Headers->GetView();
                    auto iter = view->First();
                    while (iter->MoveNext())
                    {
                        auto cur = iter->Current;
                        auto headerName = cur->Key;
                        auto headerValue = cur->Value;
                        http_internal_string aHeaderName = utf8_from_utf16(headerName->Data());
                        http_internal_string aHeaderValue = utf8_from_utf16(headerValue->Data());
                        HCHttpCallResponseSetHeader(call, aHeaderName.c_str(), aHeaderValue.c_str());
                    }

                    uwpHttpTask->m_readAsStringAsyncOp = httpResponse->Content->ReadAsStringAsync();
                    uwpHttpTask->m_readAsStringAsyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<Platform::String^, unsigned long long>(
                        [call, asyncBlock](IAsyncOperationWithProgress<Platform::String^, unsigned long long>^ asyncOp, AsyncStatus status)
                    {
                        try
                        {
                            Platform::String^ httpResponseBody = asyncOp->GetResults();
                            http_internal_string aHttpResponseBody = utf8_from_utf16(httpResponseBody->Data());
                            HCHttpCallResponseSetResponseString(call, aHttpResponseBody.c_str());
                            CompleteAsync(asyncBlock, S_OK, 0);
                        }
                        catch (Platform::Exception^ ex)
                        {
                            HRESULT errCode = (SUCCEEDED(ex->HResult)) ? S_OK : E_FAIL;
                            HCHttpCallResponseSetNetworkErrorCode(call, errCode, ex->HResult);
                            CompleteAsync(asyncBlock, ex->HResult, 0);
                        }
                    });
                }
                else
                {
                    HCHttpCallResponseSetNetworkErrorCode(call, E_FAIL, E_FAIL);
                    CompleteAsync(asyncBlock, E_FAIL, 0);
                }
            }
            catch (Platform::Exception^ ex)
            {
                HRESULT errCode = (SUCCEEDED(ex->HResult)) ? S_OK : E_FAIL;
                HCHttpCallResponseSetNetworkErrorCode(call, errCode, ex->HResult);
                CompleteAsync(asyncBlock, ex->HResult, 0);
            }
        });
    }
    catch (Platform::Exception^ ex)
    {
        HRESULT errCode = (SUCCEEDED(ex->HResult)) ? S_OK : E_FAIL;
        HCHttpCallResponseSetNetworkErrorCode(call, errCode, ex->HResult);
        CompleteAsync(asyncBlock, ex->HResult, 0);
    }
}

#endif