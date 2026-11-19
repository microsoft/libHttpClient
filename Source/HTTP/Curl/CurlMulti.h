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

    // Asyncronously cleanup any outstanding requests
    static HRESULT CleanupAsync(HC_UNIQUE_PTR<CurlMulti> multi, XAsyncBlock* async);

private:
    CurlMulti() = default;

    void ScheduleTaskQueueCallback(std::unique_lock<std::mutex>&& lock, uint32_t delay, bool isKick = false);
    static void CALLBACK TaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept;
    static void CALLBACK KickTaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept;
    static void RunTaskQueueCallback(void* context, bool canceled, bool isKick) noexcept;
    void Perform(bool canceled, bool isKick) noexcept;

    // Core curl_multi_perform + message-drain step. Requires m_mutex to be held. Does not
    // schedule any task queue work, so it is safe to call directly from a suspend handler.
    HRESULT PerformStepLocked(int& runningRequests) noexcept;

    // Fail all active requests due to unexpected CURLM or platform error
    void FailAllRequests(HRESULT hr) noexcept;
    void FailAllRequestsLocked(HRESULT hr) noexcept;

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);

    CURLM* m_curlMultiHandle{ nullptr };
    XTaskQueueHandle m_queue{ nullptr };
    std::mutex m_mutex;
    http_internal_map<CURL*, HC_UNIQUE_PTR<CurlEasyRequest>> m_easyRequests;
    bool m_performScheduled{ false };
    bool m_kickScheduled{ false };
    uint32_t m_taskQueueCallbacksPending{ 0 };
    XAsyncBlock* m_cleanupAsyncBlock{ nullptr };
};

} // httpclient
} // xbox
