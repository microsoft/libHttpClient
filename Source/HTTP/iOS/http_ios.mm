// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "http_ios.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

ios_http::ios_http(_In_ AsyncBlock* asyncBlock, _In_ hc_call_handle_t call) :
    m_call(call),
    m_asyncBlock(asyncBlock),
    m_sessionTask(nil)
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
    
    m_session = [NSURLSession sessionWithConfiguration:configuration];
}

void ios_http::completion_handler(NSData* data, NSURLResponse* response, NSError* error)
{
    assert([response isKindOfClass:[NSHTTPURLResponse class]]);
    NSHTTPURLResponse* httpResponse = (NSHTTPURLResponse*)response;
    
    if (error)
    {
        // TODO: handle errors
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return;
    }

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

    HCHttpCallResponseSetResponseBodyBytes(m_call, static_cast<const uint8_t*>([data bytes]), [data length]);
    CompleteAsync(m_asyncBlock, S_OK, 0);
}

void ios_http::initiate_request()
{
    char const* urlCString = nullptr;
    char const* methodCString = nullptr;
    if (FAILED(HCHttpCallRequestGetUrl(m_call, &methodCString, &urlCString)))
    {
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return; //TODO: handle errors better
    }
    
    NSString* urlString = [[NSString alloc] initWithUTF8String:urlCString];
    NSURL* url = [NSURL URLWithString:urlString];
    
    NSString* methodString = [[NSString alloc] initWithUTF8String:methodCString];
    
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    [request setHTTPMethod:methodString];
    
    uint32_t numHeaders = 0;
    if (FAILED(HCHttpCallRequestGetNumHeaders(m_call, &numHeaders)))
    {
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return; //TODO: errors
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
    
    uint8_t const* requestBody = nullptr;
    uint32_t requestBodySize = 0;
    if (FAILED(HCHttpCallRequestGetRequestBodyBytes(m_call, &requestBody, &requestBodySize)))
    {
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return; //TODO: errors
    }
    
    if (requestBodySize == 0)
    {
        m_sessionTask = [m_session dataTaskWithRequest:request completionHandler:
                         ^(NSData* data, NSURLResponse* response, NSError* error)
                         {
                             std::unique_ptr<ios_http> me{this};
                             me->completion_handler(data, response, error);
                         }];
    }
    else
    {
        NSData* data = [NSData dataWithBytes:requestBody length:requestBodySize];
        
        m_sessionTask = [m_session uploadTaskWithRequest:request fromData:data completionHandler:
                         ^(NSData* data, NSURLResponse* response, NSError* error)
                         {
                             std::unique_ptr<ios_http> me{this};
                             me->completion_handler(data, response, error);
                         }];
    }
    
    [m_sessionTask resume];
}

NAMESPACE_XBOX_HTTP_CLIENT_END

void Internal_HCHttpCallPerform(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
)
{
    std::unique_ptr<xbox::httpclient::ios_http> httpTask(new xbox::httpclient::ios_http(asyncBlock, call));
    HCHttpCallSetContext(call, &httpTask);
    httpTask->initiate_request();
    httpTask.release();
}
