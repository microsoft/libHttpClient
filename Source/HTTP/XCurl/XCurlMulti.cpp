#include "pch.h"
#include "XCurlMulti.h"
#include "XCurlProvider.h"

namespace xbox
{
namespace http_client
{

Result<HC_UNIQUE_PTR<XCurlMulti>> XCurlMulti::Initialize(XTaskQueuePortHandle workPort)
{
    assert(workPort);

    http_stl_allocator<XCurlMulti> a{};
    HC_UNIQUE_PTR<XCurlMulti> multi{ new (a.allocate(1)) XCurlMulti };

    multi->m_curlMultiHandle = curl_multi_init();
    if (!multi->m_curlMultiHandle)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "XCurlMulti::Initialize: curl_multi_init failed");
        return E_FAIL;
    }

    RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &multi->m_queue));

    return Result<HC_UNIQUE_PTR<XCurlMulti>>{ std::move(multi) };
}

XCurlMulti::~XCurlMulti()
{
    if (m_queue)
    {
        XTaskQueueTerminate(m_queue, true, nullptr, nullptr);
        XTaskQueueCloseHandle(m_queue);
    }

    if (!m_easyRequests.empty())
    {
        HC_TRACE_WARNING(HTTPCLIENT, "XCurlMulti::~XCurlMulti: Failing all active requests.");
        FailAllRequests(E_UNEXPECTED);
    }

    if (m_curlMultiHandle)
    {
        curl_multi_cleanup(m_curlMultiHandle);
    }
}

HRESULT XCurlMulti::AddRequest(HC_UNIQUE_PTR<XCurlEasyRequest>&& easyRequest)
{
    std::unique_lock<std::mutex> lock{ m_mutex };

    auto result = curl_multi_add_handle(m_curlMultiHandle, easyRequest->Handle());
    if (result != CURLM_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "XCurlMulti::AddRequest: curl_multi_add_handle failed with CURLCode=%u", result);
        return HrFromCurlm(result);
    }

    m_easyRequests.emplace(easyRequest->Handle(), std::move(easyRequest));

    RETURN_IF_FAILED(XTaskQueueSubmitCallback(m_queue, XTaskQueuePort::Work, this, XCurlMulti::TaskQueueCallback));

    return S_OK;
}

void CALLBACK XCurlMulti::TaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept
{
    if (!canceled)
    {
        auto multi = static_cast<XCurlMulti*>(context);
        HRESULT hr = multi->Perform();
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "XCurlMulti::Perform failed. Failing all active requests.");
            multi->FailAllRequests(hr);
        }
    }
}

HRESULT XCurlMulti::Perform() noexcept
{
    std::unique_lock<std::mutex> lock{ m_mutex };

    int runningRequests{ 0 };
    CURLMcode result = curl_multi_perform(m_curlMultiHandle, &runningRequests);
    if (result != CURLM_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "XCurlMulti::Perform: curl_multi_perform failed with CURLCode=%u", result);
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
                    HC_TRACE_ERROR(HTTPCLIENT, "XCurlMulti::Perform: curl_multi_remove_handle failed with CURLCode=%u", result);
                }

                requestIter->second->Complete(message->data.result);
                m_easyRequests.erase(requestIter);
            }
            break;
            case CURLMSG_NONE:
            case CURLMSG_LAST:
            default:
            {
                HC_TRACE_ERROR(HTTPCLIENT, "XCurlMulti::Perform: Unrecognized CURLMsg!");
                assert(false);
            }
            break;
            }
        }
    }

    if (runningRequests)
    {
        // Reschedule Perform if there are still running requests
        // TODO should we delay next callback?
        RETURN_IF_FAILED(XTaskQueueSubmitCallback(m_queue, XTaskQueuePort::Work, this, XCurlMulti::TaskQueueCallback));
    }

    return S_OK;
}

void XCurlMulti::FailAllRequests(HRESULT hr) noexcept
{
    std::unique_lock<std::mutex> lock{ m_mutex };

    for (auto& pair : m_easyRequests)
    {
        auto result = curl_multi_remove_handle(m_curlMultiHandle, pair.first);
        if (FAILED(HrFromCurlm(result)))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "XCurlMulti::FailAllRequests: curl_multi_remove_handle failed with CURLCode=%u", result);
        }
        pair.second->Fail(hr);
    }
    m_easyRequests.clear();
}

} // http_client
} // xbox
