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
    static Result<HC_UNIQUE_PTR<CurlProvider>> Initialize();
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

    static HRESULT CleanupAsync(HC_UNIQUE_PTR<CurlProvider> provider, XAsyncBlock* async) noexcept;

private:
    CurlProvider() = default;

    HRESULT PerformAsync(HCCallHandle hcCall, XAsyncBlock* async) noexcept;

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data) noexcept;
    static void CALLBACK MultiCleanupComplete(_Inout_ struct XAsyncBlock* asyncBlock) noexcept;

    // Create an CurlMulti per work port
    http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::httpclient::CurlMulti>> m_curlMultis{};

    std::mutex m_mutex;
    XAsyncBlock* m_cleanupAsyncBlock{ nullptr };
    http_internal_vector<XAsyncBlock> m_multiCleanupAsyncBlocks;
    XTaskQueueHandle m_multiCleanupQueue{ nullptr };
    size_t m_cleanupTasksRemaining{ 0 };
};

} // httpclient
} // xbox
