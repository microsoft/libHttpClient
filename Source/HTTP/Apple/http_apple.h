// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#import <Foundation/Foundation.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class http_task_apple
{
public:
    http_task_apple(_Inout_ XAsyncBlock* asyncBlock, _In_ HCCallHandle call);
    bool initiate_request();

private:
    void completion_handler(NSData* data, NSURLResponse* response, NSError* error);
    
    HCCallHandle m_call; // non owning
    XAsyncBlock* m_asyncBlock; // non owning
    
    NSURLSession* m_session;
    NSURLSessionTask* m_sessionTask;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
