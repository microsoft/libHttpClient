// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if HC_UNITTEST_API
#include "httpClient/httpClient.h"
#include "../global/global.h"

HRESULT Internal_InitializeHttpPlatform(HCInitArgs* args, PerformEnv& performEnv) noexcept
{
    // No-op
    assert(args == nullptr);
    assert(performEnv == nullptr);
    return S_OK;
}

void Internal_CleanupHttpPlatform(HC_PERFORM_ENV* performEnv) noexcept
{
    assert(!performEnv);
}

void CALLBACK Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept
{
    assert(call);
    assert(asyncBlock);
    assert(!context);
    assert(!env);
    XAsyncComplete(asyncBlock, S_OK, 0);
}
#endif