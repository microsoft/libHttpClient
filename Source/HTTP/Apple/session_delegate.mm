// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#import "session_delegate.h"

@implementation SessionDelegate
{
    HCCallHandle _call;
    void(^_completionHandler)(NSURLResponse* response, NSError* error);
}

+ (SessionDelegate*) sessionDelegateWithHCCallHandle:(HCCallHandle) call andCompletionHandler:(void(^)(NSURLResponse* response, NSError* error)) completionHandler
{
    return [[SessionDelegate alloc] initWithHCCallHandle: call andCompletionHandler:completionHandler];
}

- (instancetype) initWithHCCallHandle:(HCCallHandle)call andCompletionHandler:(void(^)(NSURLResponse*, NSError*)) completionHandler
{
    if (self = [super init])
    {
        _call = call;
        _completionHandler = completionHandler;
        return self;
    }
    return nil;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    _completionHandler([task response], error);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data
{
    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    if (HCHttpCallResponseGetResponseBodyWriteFunction(_call, &writeFunction) != S_OK ||
        writeFunction == nullptr)
    {
        [task cancel];
        return;
    }

    try
    {
        __block size_t totalBytes = 0;
        [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange byteRange, BOOL* stop) {
            size_t bytesRead = 0;
            HRESULT hr = writeFunction(_call, static_cast<const uint8_t*>(bytes), static_cast<size_t>(byteRange.length), &bytesRead);
            if (FAILED(hr) || bytesRead < byteRange.length)
            {
                *stop = YES;
            }
            totalBytes += bytesRead;
        }];

        // make sure we've consumed all the available data
        if (totalBytes < [data length])
        {
            [task cancel];
            return;
        }
    }
    catch (...)
    {
        [task cancel];
        return;
    }
}
@end
