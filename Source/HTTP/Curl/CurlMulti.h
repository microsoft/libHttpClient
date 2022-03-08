#pragma once

#include "CurlEasyRequest.h"
#include "Result.h"

namespace xbox
{
namespace httpclient
{

class CurlMulti
{
public:
    static Result<HC_UNIQUE_PTR<CurlMulti>> Initialize(XTaskQueuePortHandle workPort);
    CurlMulti(const CurlMulti&) = delete;
    CurlMulti(CurlMulti&&) = delete;
    CurlMulti& operator=(const CurlMulti&) = delete;
    ~CurlMulti();

    // Wrapper around curl_multi_add_handle
    HRESULT AddRequest(HC_UNIQUE_PTR<CurlEasyRequest> easyRequest);

    // Asyncronously cleanup and outstanding requests
    static HRESULT CleanupAsync(HC_UNIQUE_PTR<CurlMulti> multi, XAsyncBlock* async);

private:
    CurlMulti() = default;

    static void CALLBACK TaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept;
    HRESULT Perform() noexcept;

    // Fail all active requests due to unexpected CURLM or platform error
    void FailAllRequests(HRESULT hr) noexcept;

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK TaskQueueTerminated(void* context);

    CURLM* m_curlMultiHandle{ nullptr };
    XTaskQueueHandle m_queue{ nullptr };
    std::mutex m_mutex;
    http_internal_map<CURL*, HC_UNIQUE_PTR<CurlEasyRequest>> m_easyRequests;
    XAsyncBlock* m_cleanupAsyncBlock{ nullptr };
};

} // httpclient
} // xbox
