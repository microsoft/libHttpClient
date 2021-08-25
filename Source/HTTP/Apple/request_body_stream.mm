// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#import "request_body_stream.h"

@implementation RequestBodyStream
{
    HCCallHandle _call;
    size_t _offset;

    NSStreamStatus _streamStatus;
    id<NSStreamDelegate> _delegate;
}

+ (RequestBodyStream*)requestBodyStreamWithHCCallHandle:(HCCallHandle)call
{
    return [[RequestBodyStream alloc] initWithHCCallHandle:call];
}

- (instancetype)initWithHCCallHandle:(HCCallHandle)call
{
    if (self = [super init])
    {
        _call = call;
        _offset = 0;
        _streamStatus = NSStreamStatusNotOpen;
        _delegate = nil;
        return self;
    }
    return nil;
}

- (void)open
{
    _streamStatus = NSStreamStatusOpen;
}

- (void)close
{
    _streamStatus = NSStreamStatusClosed;
}

- (NSInteger)read:(uint8_t*)buffer maxLength:(NSUInteger)len
{
    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t requestBodySize = 0;
    void* context = nullptr;
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(_call, &readFunction, &requestBodySize, &context);
    if (FAILED(hr) || readFunction == nullptr)
    {
        return 0;
    }

    if (_offset >= requestBodySize)
    {
        // Tell the OS that we are done reading
        return 0;
    }

    size_t bytesWritten = 0;
    try
    {
        hr = readFunction(_call, _offset, static_cast<size_t>(len), context, buffer, &bytesWritten);
        if (FAILED(hr))
        {
            return 0;
        }
    }
    catch (...)
    {
        return 0;
    }

    _offset += bytesWritten;
    return (NSInteger)bytesWritten;
}

- (BOOL)getBuffer:(uint8_t* _Nullable *)buffer length:(NSUInteger*)len
{
    return NO;
}

- (BOOL)hasBytesAvailable
{
    return YES;
}

- (id<NSStreamDelegate>)delegate
{
    return _delegate;
}

- (void)setDelegate:(id<NSStreamDelegate>)delegate
{
    _delegate = delegate;
}

- (void)scheduleInRunLoop:(NSRunLoop *)aRunLoop forMode:(NSRunLoopMode)mode
{
    // This stream does not need a run loop to produce its data, nothing to do here
}

- (void)removeFromRunLoop:(NSRunLoop *)aRunLoop forMode:(NSRunLoopMode)mode
{
    // This stream do esnot need a run loop to produce its data, nothing to do here
}

- (id)propertyForKey:(NSStreamPropertyKey)key
{
    return nil;
}

- (BOOL)setProperty:(id)property forKey:(NSStreamPropertyKey)key
{
    return NO;
}

- (NSStreamStatus) streamStatus
{
    return _streamStatus;
}

- (NSError*) streamError
{
    return nil;
}
@end
