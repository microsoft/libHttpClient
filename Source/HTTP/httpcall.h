// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

struct HC_CALL
{
    HC_CALL() :
        statusCode(0),
        networkErrorCode(HC_OK),
        platformNetworkErrorCode(0),
        id(0),
        retryAllowed(false),
        timeoutInSeconds(0),
        timeoutWindowInSeconds(0),
        retryDelayInSeconds(0),
        enableAssertsForThrottling(false),
        performCalled(false),
        refCount(1)
    {
    }

    http_internal_string method;
    http_internal_string url;
    http_internal_vector<uint8_t> requestBodyBytes;
    http_internal_string requestBodyString;
    http_internal_map<http_internal_string, http_internal_string> requestHeaders;

    http_internal_string responseString;
    http_internal_map<http_internal_string, http_internal_string> responseHeaders;
    uint32_t statusCode;
    HC_RESULT networkErrorCode;
    uint32_t platformNetworkErrorCode;
    std::shared_ptr<xbox::httpclient::hc_task> task;
    uint64_t id;
    std::atomic<int> refCount;

    bool retryAllowed;
    uint32_t timeoutInSeconds;
    uint32_t timeoutWindowInSeconds;
    uint32_t retryDelayInSeconds;
    bool enableAssertsForThrottling;
    bool performCalled;
};

void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call, 
    _In_ HC_TASK_HANDLE taskHandle
    );


