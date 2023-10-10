#pragma once

#include "Platform/IHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class XmlHttpProvider : public IHttpProvider
{
public:
    XmlHttpProvider() = default;
    XmlHttpProvider(XmlHttpProvider const&) = default;
    XmlHttpProvider& operator=(XmlHttpProvider const&) = default;
    ~XmlHttpProvider() = default;

    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept override;
};

NAMESPACE_XBOX_HTTP_CLIENT_END