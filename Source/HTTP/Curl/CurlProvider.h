#pragma once

#include "Platform/IHttpProvider.h"
#include "CurlMulti.h"
#include "Result.h"
#if HC_PLATFORM == HC_PLATFORM_GDK
// When developing titles for Xbox consoles, you must use WinHTTP or xCurl.
// See https://docs.microsoft.com/en-us/gaming/gdk/_content/gc/networking/overviews/web-requests/http-networking for detail
#include <XCurl.h>
#include "CurlDynamicLoader.h"
#else
// This http provider should work with other curl implementations as well. 
// The logic in CurlMulti::Perform is optimized for XCurl, but should work on any curl implementation.
#include <curl/curl.h>
#endif

namespace xbox
{
namespace httpclient
{

HRESULT HrFromCurle(CURLcode c) noexcept;
HRESULT HrFromCurlm(CURLMcode c) noexcept;

struct CurlProvider : public IHttpProvider
{
public:
    static Result<HC_UNIQUE_PTR<CurlProvider>> Initialize();
    CurlProvider(const CurlProvider&) = delete;
    CurlProvider(CurlProvider&&) = delete;
    CurlProvider& operator=(const CurlProvider&) = delete;
    virtual ~CurlProvider();

    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT CleanupAsync(XAsyncBlock* async) noexcept override;

protected:
    CurlProvider() = default;

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
