#pragma once

#include "winhttp_http_task.h"

struct HC_PERFORM_ENV
{
public:
    HC_PERFORM_ENV();
    HC_PERFORM_ENV(const HC_PERFORM_ENV&) = delete;
    HC_PERFORM_ENV(HC_PERFORM_ENV&&) = delete;
    HC_PERFORM_ENV& operator=(const HC_PERFORM_ENV&) = delete;
    virtual ~HC_PERFORM_ENV() = default;

    HRESULT Perform(HCCallHandle hcCall, XAsyncBlock* async) noexcept;

    std::shared_ptr<xbox::httpclient::WinHttpState> const winHttpState;
};
