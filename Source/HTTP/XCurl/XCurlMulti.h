#pragma once

#include <XCurl.h>
#include "XCurlEasyRequest.h"
#include "Result.h"

namespace xbox
{
namespace http_client
{

class XCurlMulti
{
public:
    static Result<HC_UNIQUE_PTR<XCurlMulti>> Initialize(XTaskQueuePortHandle workPort);
    XCurlMulti(const XCurlMulti&) = delete;
    ~XCurlMulti();

    // Wrapper around curl_multi_add_handle
    HRESULT AddRequest(HC_UNIQUE_PTR<XCurlEasyRequest>&& easyRequest);

private:
    XCurlMulti() = default;

    static void CALLBACK TaskQueueCallback(_In_opt_ void* context, _In_ bool canceled) noexcept;
    HRESULT Perform() noexcept;

    // Fail all active requests due to unexpected CURLM or platform error
    void FailAllRequests(HRESULT hr) noexcept;

    CURLM* m_curlMultiHandle{ nullptr };
    XTaskQueueHandle m_queue{ nullptr };
    std::mutex m_mutex;
    http_internal_map<CURL*, HC_UNIQUE_PTR<XCurlEasyRequest>> m_easyRequests;
};

} // http_client
} // xbox
