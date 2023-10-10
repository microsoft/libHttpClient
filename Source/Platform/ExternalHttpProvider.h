#pragma once

#include <httpClient/httpProvider.h>
#include "IHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class ExternalHttpProvider : public IHttpProvider
{
public:
    static ExternalHttpProvider& Get() noexcept;

    HRESULT SetCallback(
        HCCallPerformFunction performFunc,
        void* context
    ) noexcept;

    HRESULT GetCallback(
        HCCallPerformFunction* performFunc,
        void** context
    ) const noexcept;

    bool HasCallback() const noexcept;

public: // IHttpProvider
    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept override;

    ExternalHttpProvider(ExternalHttpProvider const&) = delete;
    ExternalHttpProvider& operator=(ExternalHttpProvider const&) = delete;
    ~ExternalHttpProvider() = default;

private:
    ExternalHttpProvider() = default;

    HCCallPerformFunction m_perform{ nullptr };
    void* m_context{ nullptr };
};

NAMESPACE_XBOX_HTTP_CLIENT_END
