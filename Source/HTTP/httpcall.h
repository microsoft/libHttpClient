// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "threadpool.h"
#include "asyncop.h"
#include "mem.h"
#include "utils.h"

struct HC_CALL
{
    http_internal_string method;
    http_internal_string url;
    http_internal_string requestBodyString;
    http_internal_map(http_internal_string, http_internal_string) requestHeaders;
    bool retryAllowed;
    uint32_t timeoutInSeconds;

    http_internal_string responseString;
    http_internal_map(http_internal_string, http_internal_string) responseHeaders;
    uint32_t statusCode;
    uint32_t errorCode;
    http_internal_string errorMessage;
};

void Internal_HCHttpCallPerform(_In_ HC_CALL_HANDLE call);
