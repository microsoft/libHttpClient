#pragma once

#include "Platform/IHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class PlatformComponents_Android;

class AndroidHttpProvider : public IHttpProvider
{
public:
    AndroidHttpProvider(SharedPtr<PlatformComponents_Android> platformComponents);
    AndroidHttpProvider(AndroidHttpProvider const&) = delete;
    AndroidHttpProvider& operator=(AndroidHttpProvider const&) = delete;
    virtual ~AndroidHttpProvider() = default;

    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept override;

private:
    SharedPtr<PlatformComponents_Android> m_platformComponents;
};

NAMESPACE_XBOX_HTTP_CLIENT_END