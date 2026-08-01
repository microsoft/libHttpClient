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

    // Drives curl_multi_perform on the *calling* thread until every active request has
    // completed or timeoutMs elapses.
    //
    // The normal perform loop only advances when the caller-supplied task queue is being
    // dispatched (see ScheduleTaskQueueCallback). During an app suspend a title is free to
    // park its own queue, which would otherwise stall the loop and leave xCurl blocked in
    // Curl_multi::WaitForActiveHandles until the suspend watchdog terminates the title
    // (bug 63050439). The xCurl contract puts the "keep performing" duty on the multi
    // consumer, so on suspend LHC drives the loop itself instead of relying on that queue.
    HRESULT PerformUntilDrained(uint32_t timeoutMs) noexcept;

    // Number of requests still owned by this multi handle.
    size_t ActiveRequestCount() noexcept;

    // Asyncronously cleanup any outstanding requests
    static HRESULT CleanupAsync(HC_UNIQUE_PTR<CurlMulti> multi, XAsyncBlock* async);

private:
    CurlMulti() = default;

    void ScheduleTaskQueueCallback(std::unique_lock<std::mutex>&& lock, uint32_t delay);
    static void CALLBACK TaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept;
    HRESULT Perform() noexcept;

    // Core curl_multi_perform + message-drain step. Requires m_mutex to be held. Does not
    // schedule any task queue work, so it is safe to call directly from a suspend handler.
    HRESULT PerformStepLocked(int& runningRequests) noexcept;

    // Fail all active requests due to unexpected CURLM or platform error
    void FailAllRequests(HRESULT hr) noexcept;

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);

    CURLM* m_curlMultiHandle{ nullptr };
    XTaskQueueHandle m_queue{ nullptr };
    std::mutex m_mutex;
    http_internal_map<CURL*, HC_UNIQUE_PTR<CurlEasyRequest>> m_easyRequests;
    uint32_t m_taskQueueCallbacksPending{ 0 };
    XAsyncBlock* m_cleanupAsyncBlock{ nullptr };
};

} // httpclient
} // xbox
