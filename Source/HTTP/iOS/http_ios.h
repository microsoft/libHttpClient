// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#import <Foundation/Foundation.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class ios_http_task
{
public:
    ios_http_task(_In_ AsyncBlock* asyncBlock, _In_ hc_call_handle_t call);
    bool initiate_request();

private:
    void completion_handler(NSData* data, NSURLResponse* response, NSError* error);
    
    hc_call_handle_t m_call; // non owning
    AsyncBlock* m_asyncBlock; // non owning
    
    NSURLSession* m_session;
    NSURLSessionTask* m_sessionTask;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
