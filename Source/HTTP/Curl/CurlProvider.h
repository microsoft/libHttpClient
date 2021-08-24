#pragma once

#include "CurlMulti.h"
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
    HC_PERFORM_ENV(const HC_PERFORM_ENV&) = delete;
    HC_PERFORM_ENV(HC_PERFORM_ENV&&) = delete;
    HC_PERFORM_ENV& operator=(const HC_PERFORM_ENV&) = delete;
    virtual ~HC_PERFORM_ENV();

    HRESULT Perform(HCCallHandle hcCall, XAsyncBlock* async) noexcept;

private:
    HC_PERFORM_ENV() = default;

    // Create an CurlMulti per work port
    http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::http_client::CurlMulti>> m_curlMultis{};
};
