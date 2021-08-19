#pragma once

#include <XCurl.h>
#include "XCurlMulti.h"
#include "Result.h"

namespace xbox
{
namespace http_client
{

HRESULT HrFromCurle(CURLcode c) noexcept;
HRESULT HrFromCurlm(CURLMcode c) noexcept;

} // http_client
} // xbox

struct HC_PERFORM_ENV
{
public:
    static Result<PerformEnv> Initialize();
    virtual ~HC_PERFORM_ENV();

    HRESULT Perform(HCCallHandle hcCall, XAsyncBlock* async) noexcept;

private:
    HC_PERFORM_ENV() = default;

    // Create an XCurlMulti per work port
    http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::http_client::XCurlMulti>> m_curlMultis{};
};
