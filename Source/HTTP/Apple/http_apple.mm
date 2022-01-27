// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#import <Foundation/Foundation.h>
#include "http_apple.h"
#include "request_body_stream.h"
#include "session_delegate.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class http_task_apple
{
public:
    http_task_apple(_Inout_ XAsyncBlock* asyncBlock, _In_ HCCallHandle call);
    bool initiate_request();

private:
    void completion_handler(NSURLResponse* response, NSError* error);
    
    HCCallHandle m_call; // non owning
    XAsyncBlock* m_asyncBlock; // non owning
    
    NSURLSession* m_session;
    NSURLSessionTask* m_sessionTask;
};

http_task_apple::http_task_apple(_Inout_ XAsyncBlock* asyncBlock, _In_ HCCallHandle call) :
    m_call(call),
    m_asyncBlock(asyncBlock),
    m_sessionTask(nullptr)
{
    NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;

    uint32_t timeoutInSeconds = 0;
    if (FAILED(HCHttpCallRequestGetTimeout(m_call, &timeoutInSeconds)))
    {
        // default to 60 to match other default ios behaviour
        timeoutInSeconds = 60;
    }

    [configuration setTimeoutIntervalForRequest:(NSTimeInterval)timeoutInSeconds];
    [configuration setTimeoutIntervalForResource:(NSTimeInterval)timeoutInSeconds];

    SessionDelegate* delegate = [SessionDelegate sessionDelegateWithHCCallHandle:m_call andCompletionHandler:^(NSURLResponse *response, NSError *error) {
        std::unique_ptr<http_task_apple> me{this};
        me->completion_handler(response, error);
    }];
    m_session = [NSURLSession sessionWithConfiguration:configuration delegate:delegate delegateQueue:nil];
}

void http_task_apple::completion_handler(NSURLResponse* response, NSError* error)
{
    if (error)
    {
        uint32_t errorCode = static_cast<uint32_t>([error code]);
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %u] error from NSURLRequest code: %u", HCHttpCallGetId(m_call), errorCode);
        HRESULT errorResult = E_FAIL;
        if ([[error domain] isEqualToString:NSURLErrorDomain] && [error code] == NSURLErrorNotConnectedToInternet)
        {
            errorResult = E_HC_NO_NETWORK;
        }

        HCHttpCallResponseSetNetworkErrorCode(m_call, errorResult, errorCode);
        XAsyncComplete(m_asyncBlock, errorResult, 0);
        return;
    }

    assert([response isKindOfClass:[NSHTTPURLResponse class]]);
    NSHTTPURLResponse* httpResponse = (NSHTTPURLResponse*)response;

    uint32_t statusCode = static_cast<uint32_t>([httpResponse statusCode]);

    HCHttpCallResponseSetStatusCode(m_call, statusCode);

    NSDictionary* headers = [httpResponse allHeaderFields];
    for (NSString* key in headers)
    {
        NSString* value = headers[key];

        char const* keyCString = [key cStringUsingEncoding:NSUTF8StringEncoding];
        char const* valueCString = [value cStringUsingEncoding:NSUTF8StringEncoding];
        HCHttpCallResponseSetHeader(m_call, keyCString, valueCString);
    }

    XAsyncComplete(m_asyncBlock, S_OK, 0);
}

bool http_task_apple::initiate_request()
{
    char const* urlCString = nullptr;
    char const* methodCString = nullptr;
    if (FAILED(HCHttpCallRequestGetUrl(m_call, &methodCString, &urlCString)))
    {
        HCHttpCallResponseSetNetworkErrorCode(m_call, E_FAIL, 0);
        XAsyncComplete(m_asyncBlock, E_FAIL, 0);
        return false;
    }

    NSString* urlString = [[NSString alloc] initWithUTF8String:urlCString];
    NSURL* url = [NSURL URLWithString:urlString];

    NSString* methodString = [[NSString alloc] initWithUTF8String:methodCString];

    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    [request setHTTPMethod:methodString];

    uint32_t numHeaders = 0;
    if (FAILED(HCHttpCallRequestGetNumHeaders(m_call, &numHeaders)))
    {
        HCHttpCallResponseSetNetworkErrorCode(m_call, E_FAIL, 0);
        XAsyncComplete(m_asyncBlock, E_FAIL, 0);
        return false;
    }

    for (uint32_t i = 0; i<numHeaders; ++i)
    {
        char const* headerName;
        char const* headerValue;
        if (SUCCEEDED(HCHttpCallRequestGetHeaderAtIndex(m_call, i, &headerName, &headerValue)))
        {
            NSString* headerNameString = [[NSString alloc] initWithUTF8String:headerName];
            NSString* headerValueString = [[NSString alloc] initWithUTF8String:headerValue];

            [request addValue:headerValueString forHTTPHeaderField:headerNameString];
        }
    }

    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t requestBodySize = 0;
    void* context = nullptr;
    if (FAILED(HCHttpCallRequestGetRequestBodyReadFunction(m_call, &readFunction, &requestBodySize, &context))
        || readFunction == nullptr)
    {
        HCHttpCallResponseSetNetworkErrorCode(m_call, E_FAIL, 0);
        XAsyncComplete(m_asyncBlock, E_FAIL, 0);
        return false;
    }

    if (requestBodySize > 0)
    {
        [request setHTTPBodyStream:[RequestBodyStream requestBodyStreamWithHCCallHandle:m_call]];
        [request addValue:[NSString stringWithFormat:@"%zu", requestBodySize] forHTTPHeaderField:@"Content-Length"];
    }

    m_sessionTask = [m_session dataTaskWithRequest:request];
    [m_sessionTask resume];
    return true;
}

void AppleHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept
{
    assert(context == nullptr);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(env);

    std::unique_ptr<xbox::httpclient::http_task_apple> httpTask(new xbox::httpclient::http_task_apple(asyncBlock, call));
    HCHttpCallSetContext(call, &httpTask);
    if (httpTask->initiate_request())
    {
         httpTask.release();
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_END
