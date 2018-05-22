// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

typedef struct HC_CALL
{
    HC_CALL() :
        statusCode(0),
        networkErrorCode(S_OK),
        platformNetworkErrorCode(0),
        id(0),
        traceCall(true),
        refCount(1),
        retryAllowed(false),
        retryAfterCacheId(0),
        timeoutInSeconds(0),
        timeoutWindowInSeconds(0),
        retryDelayInSeconds(0),
        performCalled(false)
    {
        delayBeforeRetry = std::chrono::milliseconds(0);
    }

    http_internal_string method;
    http_internal_string url;
    http_internal_vector<uint8_t> requestBodyBytes;
    http_internal_string requestBodyString;
    http_internal_map<http_internal_string, http_internal_string> requestHeaders;

    http_internal_string responseString;
    http_internal_vector<uint8_t> responseBodyBytes;
    http_internal_map<http_internal_string, http_internal_string> responseHeaders;
    uint32_t statusCode;
    HRESULT networkErrorCode;
    uint32_t platformNetworkErrorCode;
    std::shared_ptr<xbox::httpclient::hc_task> task;

    uint64_t id;
    bool traceCall;
    void* context;
    std::atomic<int> refCount;

    chrono_clock_t::time_point firstRequestStartTime;
    std::chrono::milliseconds delayBeforeRetry;
    uint32_t retryIterationNumber;
    bool retryAllowed;
    uint32_t retryAfterCacheId;
    uint32_t timeoutInSeconds;
    uint32_t timeoutWindowInSeconds;
    uint32_t retryDelayInSeconds;
    bool performCalled;
} HC_CALL;

HRESULT Internal_HCHttpPlatformInitialize(void* context);

HRESULT Interal_HCHttpPlatformCleanup();

void Internal_HCHttpCallPerform(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    );


