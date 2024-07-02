// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include <httpClient/httpProvider.h>
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

+ (void) reportProgress:(HCCallHandle)call progressReportFunction:(HCHttpCallProgressReportFunction)progressReportFunction minimumInterval:(size_t)minimumInterval current:(size_t)current total:(size_t)total lastProgressReport:(std::chrono::steady_clock::time_point*)lastProgressReport
{
    if (progressReportFunction != nullptr)
    {
        long minimumProgressReportIntervalInMs = static_cast<long>(minimumInterval * 1000);

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastProgressReport).count();

        if (elapsed >= minimumProgressReportIntervalInMs)
        {
            HRESULT hr = progressReportFunction(call, (int)current, (int)total);
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ReportProgress: something went wrong after invoking the progress callback function.");
            }

            *lastProgressReport = now;
        }
    }
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
    void* context = nullptr;
    if (FAILED(HCHttpCallResponseGetResponseBodyWriteFunction(_call, &writeFunction, &context)) ||
        writeFunction == nullptr)
    {
        [task cancel];
        return;
    }

    try
    {
        __block HRESULT hr = S_OK;
        [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange byteRange, BOOL* stop) {
            hr = writeFunction(_call, static_cast<const uint8_t*>(bytes), static_cast<size_t>(byteRange.length), context);
            if (FAILED(hr))
            {
                *stop = YES;
            }
        }];

        if (FAILED(hr))
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
    
    [_dataToDownload appendData:data];
    
    HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(_call, false, &downloadProgressReportFunction);
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ProgressReportCallback: failed getting Progress Report upload function");
    }

    [SessionDelegate reportProgress:_call progressReportFunction:downloadProgressReportFunction minimumInterval:_call->downloadMinimumProgressReportInterval current:[ _dataToDownload length ] total:_downloadSize lastProgressReport:&_call->downloadLastProgressReport ];
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
   didSendBodyData:(int64_t)bytesSent
    totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend
{       
    HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(_call, true, &uploadProgressReportFunction);
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ProgressReportCallback: failed getting Progress Report upload function");
    }

    [SessionDelegate reportProgress:_call progressReportFunction:uploadProgressReportFunction minimumInterval:_call->uploadMinimumProgressReportInterval current:totalBytesSent total:totalBytesExpectedToSend lastProgressReport:&_call->uploadLastProgressReport ];
    
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler {
    completionHandler(NSURLSessionResponseAllow);

    _downloadSize=[response expectedContentLength];
    _dataToDownload=[[NSMutableData alloc]init];
}

@end
