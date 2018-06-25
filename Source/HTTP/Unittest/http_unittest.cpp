// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if HC_UNITTEST_API
#include "httpClient/httpClient.h"
#include "../global/global.h"

HRESULT IHCPlatformContext::InitializeHttpPlatformContext(void* initialContext, IHCPlatformContext** platformContext)
{
    // No-op
    assert(initialContext == nullptr);
    *platformContext = nullptr;
    return S_OK;
}

void Internal_HCHttpCallPerformAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ hc_call_handle_t call
    )
{
    CompleteAsync(asyncBlock, S_OK, 0);
}
#endif