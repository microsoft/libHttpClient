#pragma once

#include "CurlMulti.h"
#include "Result.h"

namespace xbox
{
namespace httpclient
{

HRESULT HrFromCurle(CURLcode c) noexcept;
HRESULT HrFromCurlm(CURLMcode c) noexcept;

struct CurlProvider
{
public:
    static Result<std::shared_ptr<CurlProvider>> Initialize();
    CurlProvider(const CurlProvider&) = delete;
    CurlProvider(CurlProvider&&) = delete;
    CurlProvider& operator=(const CurlProvider&) = delete;
    virtual ~CurlProvider();

    static void CALLBACK PerformAsyncHandler(
        HCCallHandle callHandle,
        XAsyncBlock* async,
        void* context,
        HCPerformEnv env
    ) noexcept;

private:
    CurlProvider() = default;

    HRESULT PerformAsync(HCCallHandle hcCall, XAsyncBlock* async) noexcept;

    // Create an CurlMulti per work port
    http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::httpclient::CurlMulti>> m_curlMultis{};
};

} // httpclient
} // xbox
