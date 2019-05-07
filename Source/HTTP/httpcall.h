// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

struct http_header_compare
{
    bool operator()(http_internal_string const& l, http_internal_string const& r) const;
};

using http_header_map = http_internal_map<http_internal_string, http_internal_string, http_header_compare>;

struct HC_CALL
{
    HC_CALL()
    {
        refCount = 1;
    }
    ~HC_CALL();

    http_internal_string method;
    http_internal_string url;
    http_internal_vector<uint8_t> requestBodyBytes;
    http_internal_string requestBodyString;
    http_header_map requestHeaders;

    http_internal_string responseString;
    http_internal_vector<uint8_t> responseBodyBytes;
    http_header_map responseHeaders;
    uint32_t statusCode = 0;
    HRESULT networkErrorCode = S_OK;
    uint32_t platformNetworkErrorCode = 0;
    http_internal_string platformNetworkErrorMessage;
    std::shared_ptr<xbox::httpclient::hc_task> task;

    uint64_t id = 0;
    bool traceCall = true;
    void* context = nullptr;
    std::atomic<int> refCount;

    chrono_clock_t::time_point firstRequestStartTime;
    std::chrono::milliseconds delayBeforeRetry = std::chrono::milliseconds(0);
    uint32_t retryIterationNumber = 0;
    bool retryAllowed = false;
    uint32_t retryAfterCacheId = 0;
    uint32_t timeoutInSeconds = 0;
    uint32_t timeoutWindowInSeconds = 0;
    uint32_t retryDelayInSeconds = 0;
    bool performCalled = false;
};

struct HttpPerformInfo
{
    HttpPerformInfo(_In_ HCCallPerformFunction h, _In_opt_ void* ctx)
        : handler(h), context(ctx)
    { }
    HCCallPerformFunction handler = nullptr;
    void* context = nullptr; // non owning
};

struct PerformEnvDeleter
{
    void operator()(HC_PERFORM_ENV* performEnv) noexcept;
};

using PerformEnv = std::unique_ptr<HC_PERFORM_ENV, PerformEnvDeleter>;

HRESULT Internal_InitializeHttpPlatform(HCInitArgs* args, PerformEnv& performEnv) noexcept;

void Internal_CleanupHttpPlatform(HC_PERFORM_ENV* performEnv) noexcept;

void CALLBACK Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept;
