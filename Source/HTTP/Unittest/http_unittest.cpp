// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if HC_UNITTEST_API
#include "httpClient/httpClient.h"
#include "../global/global.h"

HRESULT IHCPlatformContext::InitializeHttpPlatformContext(HCInitArgs* args, IHCPlatformContext** platformContext)
{
    // No-op
    assert(args == nullptr);
    *platformContext = nullptr;
    return S_OK;
}

void Internal_HCHttpCallPerformAsync(
    _In_ hc_call_handle_t call,
    _Inout_ AsyncBlock* asyncBlock
    )
{
    CompleteAsync(asyncBlock, S_OK, 0);
}
#endif