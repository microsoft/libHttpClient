
// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "Platform/IHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class AppleHttpProvider : public IHttpProvider
{
public:
    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock *async
    ) noexcept override;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
