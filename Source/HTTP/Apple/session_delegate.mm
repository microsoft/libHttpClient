// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include <httpClient/httpProvider.h>
#import "session_delegate.h"

struct TaskContext
{
    HCCallHandle _call; // non owning
    long long _downloadSize;
};

@implementation SessionDelegate
{
    void(^_completionHandler)(uint32_t sessionTimeout, NSUInteger taskIdentifier, NSURLResponse* response, NSError* error);
    
    std::mutex _taskContextsMutex;
    std::unordered_map<NSUInteger, TaskContext> _taskContexts;
}

+ (SessionDelegate*) sessionDelegateWithCompletionHandler:(void(^)(uint32_t sessionTimeout, NSUInteger taskIdentifier, NSURLResponse* response, NSError* error)) completionHandler
{
    return [[SessionDelegate alloc] initWithCompletionHandler:completionHandler];
}

+ (void) reportProgress:(HCCallHandle)call progressReportFunction:(HCHttpCallProgressReportFunction)progressReportFunction minimumInterval:(size_t)minimumInterval current:(size_t)current total:(size_t)total progressReportCallbackContext:(void*)progressReportCallbackContext lastProgressReport:(std::chrono::steady_clock::time_point*)lastProgressReport
{
    if (progressReportFunction != nullptr)
    {
        long minimumProgressReportIntervalInMs = static_cast<long>(minimumInterval * 1000);

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastProgressReport).count();

        if (elapsed >= minimumProgressReportIntervalInMs)
        {
            HRESULT hr = progressReportFunction(call, (int)current, (int)total, progressReportCallbackContext);
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ReportProgress: something went wrong after invoking the progress callback function.");
            }

            *lastProgressReport = now;
        }
    }
}

- (bool) registerContextForTask:(NSUInteger)taskIdentifier withCall:(HCCallHandle)call
{
    bool registered = false;
    {
        std::unique_lock<std::mutex> uniqueLock(_taskContextsMutex);
        
        if (_taskContexts.find(taskIdentifier) == _taskContexts.end())
        {
            _taskContexts.emplace(taskIdentifier, TaskContext{ ._call = call });
            registered = true;
        }
        else
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, "Task context already exists, cannot register for identifier %lu", taskIdentifier);
        }
    }
    return registered;
}

- (instancetype) initWithCompletionHandler:(void(^)(uint32_t sessionTimeout, NSUInteger taskIdentifier, NSURLResponse* response, NSError* error)) completionHandler
{
    if (self = [super init])
    {
        _completionHandler = completionHandler;
        return self;
    }
    return nil;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error
{
    {
        std::unique_lock<std::mutex> uniqueLock(_taskContextsMutex);
        _taskContexts.erase([task taskIdentifier]);
    }
    
    _completionHandler([[session configuration] timeoutIntervalForRequest], [task taskIdentifier], [task response], error);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data
{
    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    
    TaskContext taskContext;
    bool hasContext = false;
    {
        std::unique_lock<std::mutex> uniqueLock(_taskContextsMutex);
        
        auto existingTaskContext = _taskContexts.find([task taskIdentifier]);
        if (existingTaskContext != _taskContexts.end())
        {
            hasContext = true;
            taskContext = existingTaskContext->second;
        }
    }
    
    if (!hasContext)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Task context missing for data of identifier %lu", [task taskIdentifier]);
        [task cancel];
        return;
    }
    
    if (FAILED(HCHttpCallResponseGetResponseBodyWriteFunction(taskContext._call, &writeFunction, &context)) ||
        writeFunction == nullptr)
    {
        [task cancel];
        return;
    }

    try
    {
        __block HRESULT hr = S_OK;
        [data enumerateByteRangesUsingBlock:^(const void* bytes, NSRange byteRange, BOOL* stop) {
            hr = writeFunction(taskContext._call, static_cast<const uint8_t*>(bytes), static_cast<size_t>(byteRange.length), context);
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
    
    size_t downloadMinimumProgressInterval;
    void* downloadProgressReportCallbackContext{};
    HCHttpCallProgressReportFunction downloadProgressReportFunction = nullptr;
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(taskContext._call, false, &downloadProgressReportFunction, &downloadMinimumProgressInterval, &downloadProgressReportCallbackContext);
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ProgressReportCallback: failed getting Progress Report upload function");
    }

    [SessionDelegate reportProgress:taskContext._call progressReportFunction:downloadProgressReportFunction minimumInterval:taskContext._call->downloadMinimumProgressReportInterval current:taskContext._call->responseBodyBytes.size() total:taskContext._downloadSize progressReportCallbackContext: downloadProgressReportCallbackContext lastProgressReport:&taskContext._call->downloadLastProgressReport];
}

- (void)URLSession:(NSURLSession *)session
              task:(NSURLSessionTask *)task
   didSendBodyData:(int64_t)bytesSent
    totalBytesSent:(int64_t)totalBytesSent
totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend
{
    size_t uploadMinimumProgressInterval;
    void* uploadProgressReportCallbackContext{};
    HCHttpCallProgressReportFunction uploadProgressReportFunction = nullptr;
    
    HCCallHandle call = nullptr;
    bool hasContext = false;
    {
        std::unique_lock<std::mutex> uniqueLock(_taskContextsMutex);
        
        auto existingTaskContext = _taskContexts.find([task taskIdentifier]);
        if (existingTaskContext != _taskContexts.end())
        {
            hasContext = true;
            call = existingTaskContext->second._call;
        }
    }
    
    if (!hasContext)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Task context missing for send report of identifier %lu", [task taskIdentifier]);
        [task cancel];
        return;
    }
    
    HRESULT hr = HCHttpCallRequestGetProgressReportFunction(call, true, &uploadProgressReportFunction, &uploadMinimumProgressInterval, &uploadProgressReportCallbackContext);
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlEasyRequest::ProgressReportCallback: failed getting Progress Report upload function");
    }

    [SessionDelegate reportProgress:call progressReportFunction:uploadProgressReportFunction minimumInterval:call->uploadMinimumProgressReportInterval current:totalBytesSent total:totalBytesExpectedToSend progressReportCallbackContext:uploadProgressReportCallbackContext lastProgressReport:&call->uploadLastProgressReport];
    
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)dataTask didReceiveResponse:(NSURLResponse *)response completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler {
    completionHandler(NSURLSessionResponseAllow);
    
    bool hasContext = false;
    {
        std::unique_lock<std::mutex> uniqueLock(_taskContextsMutex);
        
        auto existingTaskContext = _taskContexts.find([dataTask taskIdentifier]);
        if (existingTaskContext != _taskContexts.end())
        {
            hasContext = true;
            existingTaskContext->second._downloadSize = [response expectedContentLength];
        }
    }
    
    if (!hasContext)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Task context missing for response of identifier %lu", [dataTask taskIdentifier]);
        [dataTask cancel];
    }
}

@end
