#include "pch.h"

#include <cassert>

#include "../httpcall.h"

HRESULT Internal_InitializeHttpPlatform(
    HCInitArgs* initialContext,
    PerformInfo& performInfo,
    PerformEnv& performEnv
) noexcept
{
    return S_OK;
}

void Internal_CleanupHttpPlatform(HC_PERFORM_ENV* performEnv) noexcept
{
    assert(!performEnv);
}

void Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ AsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
) noexcept
{
    // TODO fail the async block with E_NOT_SUPPORTED
    assert(false);
}
