// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

class hc_task
{
public:
    hc_task() {}

    virtual ~hc_task() {}
};

struct HC_CALL
{
    http_internal_string method;
    http_internal_string url;
    http_internal_string requestBodyString;
    http_internal_map<http_internal_string, http_internal_string> requestHeaders;
    bool retryAllowed;
    uint32_t timeoutInSeconds;

    http_internal_string responseString;
    http_internal_map<http_internal_string, http_internal_string> responseHeaders;
    uint32_t statusCode;
    uint32_t errorCode;
    http_internal_string errorMessage;
    std::shared_ptr<hc_task> task;
    uint64_t id;
};

void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call, 
    _In_ HC_TASK_HANDLE taskHandle
    );


