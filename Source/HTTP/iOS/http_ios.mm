// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "http_ios.h"


ios_http::ios_http(_In_ AsyncBlock* asyncBlock, _In_ hc_call_handle_t call) :
m_call(call),
m_asyncBlock(asyncBlock)
{
    NSURLSessionConfiguration* configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;

    uint32_t timeoutInSeconds = 0;
    if (HCHttpCallRequestGetTimeout(m_call, &timeoutInSeconds) != S_OK)
    {
        // default to 60 to match other default ios behaviour
        timeoutInSeconds = 60;
    }
    
    [configuration setTimeoutIntervalForRequest:(NSTimeInterval)timeoutInSeconds];
    [configuration setTimeoutIntervalForResource:(NSTimeInterval)timeoutInSeconds];
    
    m_session = [NSURLSession sessionWithConfiguration:configuration];
}

void ios_http::completion_handler(NSData *data, NSURLResponse *response, NSError *error)
{
    if (error)
    {
        // TODO: handle errors better
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return;
    }
    
    if(![response isKindOfClass:[NSHTTPURLResponse class]])
    {
        // something has gone horribly wrong, we should always get a NSHTTPURLResponse
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return;
    }
    NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse*)response;
    uint32_t statusCode = (uint32_t)[httpResponse statusCode];
    
    HCHttpCallResponseSetStatusCode(m_call, statusCode);
    
    NSDictionary *headers = [httpResponse allHeaderFields];
    for (NSString *key in headers)
    {
        NSString *value = headers[key];
        
        const char *keyCString = [key cStringUsingEncoding:NSUTF8StringEncoding];
        const char *valueCString = [value cStringUsingEncoding:NSUTF8StringEncoding];
        HCHttpCallResponseSetHeader(m_call, keyCString, valueCString);
    }

    HCHttpCallResponseSetResponseBodyBytes(m_call, static_cast<const uint8_t*>([data bytes]), [data length]);
    CompleteAsync(m_asyncBlock, S_OK, 0);
}

void ios_http::initiate_request()
{
    const char* urlCString = nullptr;
    const char* methodCString = nullptr;
    if (HCHttpCallRequestGetUrl(m_call, &methodCString, &urlCString) != S_OK)
    {
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return; //TODO: actually use helpful error codes
    }
    
    NSString* urlString = [[NSString alloc] initWithUTF8String:urlCString];
    NSURL* url = [NSURL URLWithString:urlString];
    
    NSString* methodString = [[NSString alloc] initWithUTF8String:methodCString];
    
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
    [request setHTTPMethod:methodString];
    
    uint32_t numHeaders = 0;
    if (HCHttpCallRequestGetNumHeaders(m_call, &numHeaders) != S_OK)
    {
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return; //TODO add error codes
    }
    if (numHeaders > 0)
    {
        //TODO: user agent?????
        for(uint32_t i=0; i<numHeaders; ++i)
        {
            const char* headerName;
            const char* headerValue;
            if (HCHttpCallRequestGetHeaderAtIndex(m_call, i, &headerName, &headerValue) == S_OK && headerName != nullptr && headerValue != nullptr)
            {
                NSString* headerNameString = [[NSString alloc] initWithUTF8String:headerName];
                NSString* headerValueString = [[NSString alloc] initWithUTF8String:headerValue];
                
                [request addValue:headerValueString forHTTPHeaderField:headerNameString];
            }
        }
    }
    
    
    const BYTE* requestBody = nullptr;
    uint32_t requestBodySize = 0;
    if(HCHttpCallRequestGetRequestBodyBytes(m_call, &requestBody, &requestBodySize) != S_OK)
    {
        CompleteAsync(m_asyncBlock, E_FAIL, 0);
        return; //TODO: add error codes
    }
    
    if (requestBodySize == 0)
    {
        m_sessionTask = [m_session dataTaskWithRequest:request completionHandler:
                         ^(NSData *data, NSURLResponse *response, NSError *error)
                         {
                         this->completion_handler(data, response, error);
                         }];
    }
    else
    {
        NSData* data = [NSData dataWithBytes:requestBody length:requestBodySize];
        
        m_sessionTask = [m_session uploadTaskWithRequest:request fromData:data completionHandler:
                         ^(NSData *data, NSURLResponse *response, NSError *error)
                         {
                         this->completion_handler(data, response, error);
                         }];
    }
    
    [m_sessionTask resume];
}



void Internal_HCHttpCallPerform(
                                _In_ AsyncBlock* asyncBlock,
                                _In_ hc_call_handle_t call
                                )
{
    ios_http* httpTask = new ios_http(asyncBlock, call);
    HCHttpCallSetContext(call, httpTask);
    httpTask->initiate_request();
}

