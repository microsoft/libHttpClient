
// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "Platform/IHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class AppleHttpSessionManager;

class AppleHttpProvider : public IHttpProvider
{
public:
    AppleHttpProvider();
    ~AppleHttpProvider();
    
    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock *async
    ) noexcept override;
    
private:
    std::shared_ptr<AppleHttpSessionManager> m_httpSessionManager;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
