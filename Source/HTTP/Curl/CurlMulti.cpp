#include "pch.h"
#include "CurlMulti.h"
#include "CurlProvider.h"

namespace xbox
{
namespace httpclient
{

// XCurl doesn't support curl_multi_timeout, so use a small, fixed delay between calls to curl_multi_perform
#define PERFORM_DELAY_MS 50
#define POLL_TIMEOUT_MS 0

Result<HC_UNIQUE_PTR<CurlMulti>> CurlMulti::Initialize(XTaskQueuePortHandle workPort)
{
    assert(workPort);

    http_stl_allocator<CurlMulti> a{};
    HC_UNIQUE_PTR<CurlMulti> multi{ new (a.allocate(1)) CurlMulti };

    multi->m_curlMultiHandle = curl_multi_init();
    if (!multi->m_curlMultiHandle)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::Initialize: curl_multi_init failed");
        return E_FAIL;
    }

    RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &multi->m_queue));

    return Result<HC_UNIQUE_PTR<CurlMulti>>{ std::move(multi) };
}

CurlMulti::~CurlMulti()
{
    if (m_queue)
    {
        XTaskQueueCloseHandle(m_queue);
    }

    if (!m_easyRequests.empty())
    {
        HC_TRACE_WARNING(HTTPCLIENT, "CurlMulti::~XCurlMulti: Failing all active requests.");
        FailAllRequests(E_UNEXPECTED);
    }

    if (m_curlMultiHandle)
    {
        curl_multi_cleanup(m_curlMultiHandle);
    }
}

HRESULT CurlMulti::AddRequest(HC_UNIQUE_PTR<CurlEasyRequest> easyRequest)
{
    std::unique_lock<std::mutex> lock{ m_mutex };

    if (m_cleanupAsyncBlock)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::AddRequest: request added after cleanup");
        return E_FAIL;
    }

    auto result = curl_multi_add_handle(m_curlMultiHandle, easyRequest->Handle());
    if (result != CURLM_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::AddRequest: curl_multi_add_handle failed with CURLCode=%u", result);
        return HrFromCurlm(result);
    }

    m_easyRequests.emplace(easyRequest->Handle(), std::move(easyRequest));

    // Schedule the perform callback immediately
    ScheduleTaskQueueCallback(std::move(lock), 0);

    return S_OK;
}

HRESULT CurlMulti::CleanupAsync(HC_UNIQUE_PTR<CurlMulti> multi, XAsyncBlock* async)
{
    RETURN_IF_FAILED(XAsyncBegin(async, multi.get(), __FUNCTION__, __FUNCTION__, CleanupAsyncProvider));
    multi.release();
    return S_OK;
}

HRESULT CALLBACK CurlMulti::CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    CurlMulti* multi = static_cast<CurlMulti*>(data->context);

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        std::unique_lock<std::mutex> lock{ multi->m_mutex };

        assert(multi->m_cleanupAsyncBlock == nullptr);
        multi->m_cleanupAsyncBlock = data->async;

        if (multi->m_taskQueueCallbacksPending == 0)
        {
            // No outstanding HTTP requests schedule cleanup immediately. This condition also ensures that we don't have
            // a pending Perform call scheduled.
            RETURN_IF_FAILED(XAsyncSchedule(data->async, 0));
        }
        else
        {
            // Terminate the XTaskQueue. Cleanup will be completed after completing remaining HTTP requests
            RETURN_IF_FAILED(XTaskQueueTerminate(multi->m_queue, false, nullptr, nullptr));
        }
        return S_OK;
    }
    case XAsyncOp::DoWork:
    {
        assert(multi->m_easyRequests.empty());
        HC_UNIQUE_PTR<CurlMulti> reclaim{ static_cast<CurlMulti*>(multi) };

        // Ensure CurlMulti is destroyed (and thus curl_multi_cleanup is called) before completing asyncBlock
        reclaim.reset();

        XAsyncComplete(data->async, S_OK, 0);
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CurlMulti::ScheduleTaskQueueCallback(std::unique_lock<std::mutex>&& lock, uint32_t delay)
{
    assert(lock.owns_lock());
    m_taskQueueCallbacksPending++;
    lock.unlock();

    HRESULT hr = XTaskQueueSubmitDelayedCallback(m_queue, XTaskQueuePort::Work, delay, this, CurlMulti::TaskQueueCallback);
    if (FAILED(hr))
    {
        // Treat errors scheduling the callback as cancellations by calling TaskQueueCallback so that m_taskQueueCallbacksPending
        // gets updated approrpriately
        TaskQueueCallback(this, true);

        HC_TRACE_WARNING_HR(HTTPCLIENT, hr, "CurlMulti::ScheduleTaskQueueCallback: XTaskQueueSubmitDelayedCallback failed");
    }
}

void CALLBACK CurlMulti::TaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept
{
    assert(context);
    auto multi = static_cast<CurlMulti*>(context);

    if (!canceled)
    {
        HRESULT hr = multi->Perform();
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlMulti::Perform failed. Failing all active requests.");
            multi->FailAllRequests(hr);
        }
    }
    else
    {
        multi->FailAllRequests(E_ABORT);
    }

    std::unique_lock<std::mutex> lock{ multi->m_mutex };
    // If CurlMulti::CleanupAsync was called and there are no remaining task queue callbacks, schedule cleanup now
    if (--multi->m_taskQueueCallbacksPending == 0 && multi->m_cleanupAsyncBlock)
    {
        HRESULT hr = XAsyncSchedule(multi->m_cleanupAsyncBlock, 0);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlMulti::TaskQueueCallback: Failed to schedule CleanupAsyncProvider!");
            assert(false);
        }
    }
}

HRESULT CurlMulti::Perform() noexcept
{
    std::unique_lock<std::mutex> lock{ m_mutex };

    int runningRequests{ 0 };
    CURLMcode result = curl_multi_perform(m_curlMultiHandle, &runningRequests);
    if (result != CURLM_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::Perform: curl_multi_perform failed with CURLCode=%u", result);
        return HrFromCurlm(result);
    }

    int remainingMessages{ 1 }; // assume there is at least 1 message so loop is always entered
    while (remainingMessages)
    {
        CURLMsg* message = curl_multi_info_read(m_curlMultiHandle, &remainingMessages);
        if (message)
        {
            switch (message->msg)
            {
            case CURLMSG_DONE:
            {
                auto requestIter = m_easyRequests.find(message->easy_handle);
                assert(requestIter != m_easyRequests.end());

                result = curl_multi_remove_handle(m_curlMultiHandle, message->easy_handle);
                if (result != CURLM_OK)
                {
                    HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::Perform: curl_multi_remove_handle failed with CURLCode=%u", result);
                }

                requestIter->second->Complete(message->data.result);
                m_easyRequests.erase(requestIter);
            }
            break;
            case CURLMSG_NONE:
            case CURLMSG_LAST:
            default:
            {
                HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::Perform: Unrecognized CURLMsg!");
                assert(false);
            }
            break;
            }
        }
    }

    if (runningRequests)
    {
        // Reschedule Perform if there are still running requests
        int workAvailable{ 0 };
#if HC_PLATFORM == HC_PLATFORM_GDK || LIBCURL_VERSION_NUM >= 0x074201
        result = curl_multi_poll(m_curlMultiHandle, nullptr, 0, POLL_TIMEOUT_MS, &workAvailable);
#else
        result = curl_multi_wait(m_curlMultiHandle, nullptr, 0, POLL_TIMEOUT_MS, &workAvailable);
#endif

        if (result != CURLM_OK)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::Perform: curl_multi_poll failed with CURLCode=%u", result);
            return HrFromCurlm(result);
        }

        uint32_t delay = workAvailable ? 0 : PERFORM_DELAY_MS;
        ScheduleTaskQueueCallback(std::move(lock), delay);
    }
    else if (m_cleanupAsyncBlock)
    {
        // CleanupAsync was called and no remaining requests, schedule cleanup now
        HRESULT hr = XAsyncSchedule(m_cleanupAsyncBlock, 0);
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "CurlMulti: Failed to schedule CleanupAsyncProvider!");
            assert(false);
        }
    }

    return S_OK;
}

void CurlMulti::FailAllRequests(HRESULT hr) noexcept
{
    std::unique_lock<std::mutex> lock{ m_mutex };

    if (!m_easyRequests.empty())
    {
        for (auto& pair : m_easyRequests)
        {
            auto result = curl_multi_remove_handle(m_curlMultiHandle, pair.first);
            if (FAILED(HrFromCurlm(result)))
            {
                HC_TRACE_ERROR(HTTPCLIENT, "CurlMulti::FailAllRequests: curl_multi_remove_handle failed with CURLCode=%u", result);
            }
            pair.second->Fail(hr);
        }
        m_easyRequests.clear();
    }
}

} // httpclient
} // xbox
