#pragma once

#include "CurlMulti.h"
#include "Result.h"

#if HC_WINHTTP_WEBSOCKETS
// XCurl provider doesn't have Websocket support, and because HC_PERFORM_ENV is shared between both Http and WebSocket
// providers, we need to include WinHttpState needed for WinHttp WebSocket implementation here
#include "../WinHttp/winhttp_http_task.h"
#endif

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
    HC_PERFORM_ENV();

    // Create an CurlMulti per work port
    http_internal_map<XTaskQueuePortHandle, HC_UNIQUE_PTR<xbox::http_client::CurlMulti>> m_curlMultis{};

#if HC_WINHTTP_WEBSOCKETS
public:
    std::shared_ptr<xbox::httpclient::WinHttpState> const winHttpState;
#endif
};
