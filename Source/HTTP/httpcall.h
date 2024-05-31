// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/httpClient.h>
#include "Platform/IHttpProvider.h"

namespace xbox
{
namespace httpclient
{

struct HeaderCompare
{
    bool operator()(http_internal_string const& l, http_internal_string const& r) const;
};

using HttpHeaders = http_internal_map<http_internal_string, http_internal_string, HeaderCompare>;

} // namesapce httpclient
} // namespace xbox

struct HC_CALL
{
public:
    HC_CALL(uint64_t id, xbox::httpclient::IHttpProvider& provider);
    HC_CALL(const HC_CALL&) = delete;
    HC_CALL(HC_CALL&&) = delete;
    HC_CALL& operator=(const HC_CALL&) = delete;
    virtual ~HC_CALL();

    // Entry point for HCHttpCallPerformAsync
    HRESULT PerformAsync(XAsyncBlock* async) noexcept;

    // Request ID for logging
    const uint64_t id;

    // RefCount maintained via HCHttpCallDuplicateHandle/HCHttpCallCloseHandle
    std::atomic<int> refCount{ 1 };

    // Request properties
    http_internal_string method{};
    http_internal_string url{};
    http_internal_vector<uint8_t> requestBodyBytes{};
    http_internal_string requestBodyString{};
    xbox::httpclient::HttpHeaders requestHeaders{};
    HCHttpCallRequestBodyReadFunction requestBodyReadFunction{ HC_CALL::ReadRequestBody };
    void* requestBodyReadFunctionContext{ nullptr };
    size_t requestBodySize{ 0 };
    bool traceCall{ true };
    void* context{ nullptr };
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
    bool sslValidation{ true };
#endif
    uint32_t timeoutInSeconds{ 0 };
    HCCompressionLevel compressionLevel{ HCCompressionLevel::None };

    // Response properties
    HRESULT networkErrorCode{ S_OK };
    uint32_t platformNetworkErrorCode{ 0 };
    http_internal_string platformNetworkErrorMessage{};
    uint32_t statusCode{ 0 };
    http_internal_string responseString{};
    http_internal_vector<uint8_t> responseBodyBytes{};
    xbox::httpclient::HttpHeaders responseHeaders{};
    HCHttpCallResponseBodyWriteFunction responseBodyWriteFunction{ HC_CALL::ResponseBodyWrite };
    void* responseBodyWriteFunctionContext{ nullptr };
    // Response Compression 
    // If a custom write callback is set and a compressed response is expected then we would need 
    // to 'reset' responseBodyWriteFunction to HC_CALL::ResponseBodyWrite prior to decompression.
    // This field will hold  the custom response write callback until decompression is completed
    HCHttpCallResponseBodyWriteFunction clientResponseBodyWriteFunction{ nullptr };
    // Hold a write function context that may be provided when a custom write callback 
    // is set and a compressed response is expected
    void* clientResponseBodyWriteContext{ nullptr };
    bool compressedResponse{ false };

    // Request metadata
    bool performCalled{ false };
    bool retryAllowed{ false };
    uint32_t retryAfterCacheId{ 0 };
    uint32_t timeoutWindowInSeconds{ 0 };
    uint32_t retryDelayInSeconds{ 0 };

    // Progress Report
    HCHttpCallProgressReportFunction progressReportFunction{};
    size_t progressReportFrequency{ 1 };

    //static HRESULT CALLBACK ReportProgress(uint32_t progress, void* status);

    static HRESULT CALLBACK ReadRequestBody(
        _In_ HCCallHandle call,
        _In_ size_t offset,
        _In_ size_t bytesAvailable,
        _In_opt_ void* context,
        _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
        _Out_ size_t* bytesWritten
    ) noexcept;

    static HRESULT CALLBACK ResponseBodyWrite(
        _In_ HCCallHandle call,
        _In_reads_bytes_(bytesAvailable) const uint8_t* source,
        _In_ size_t bytesAvailable,
        _In_opt_ void* context
    ) noexcept;

private:
    static HRESULT CALLBACK PerfomAsyncProvider(XAsyncOp op, XAsyncProviderData const* data);
    static void CALLBACK PerformSingleRequest(void* context, bool canceled);
    static HRESULT CALLBACK PerformSingleRequestAsyncProvider(XAsyncOp op, XAsyncProviderData const* data) noexcept;
    static void CALLBACK PerformSingleRequestComplete(XAsyncBlock* async);
    static void CALLBACK CompressRequestBody(void* context, bool canceled);

    xbox::httpclient::Result<bool> ShouldFailFast(_Out_opt_ uint32_t& performDelay);
    bool ShouldRetry(_Out_opt_ uint32_t& performDelay);
    xbox::httpclient::Result<std::chrono::seconds> GetRetryAfterHeaderTime();
    void ResetResponseProperties();

    // Retry metadata
    chrono_clock_t::time_point m_performStartTime{};
    uint32_t m_iterationNumber{ 0 };

    xbox::httpclient::IHttpProvider& m_provider;
};
