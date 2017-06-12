// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "mem.h"
#include "singleton.h"
#include "log.h"
#include "httpcall.h"

HC_API void HC_CALLING_CONV
HCHttpCallCreate(
    _Out_ HC_CALL_HANDLE* call
    )
{
    VerifyGlobalInit();

    HC_CALL* hcCall = new HC_CALL();
    hcCall->retryAllowed = true;

    *call = hcCall;
}

HC_API void HC_CALLING_CONV
HCHttpCallCleanup(
    _In_ HC_CALL_HANDLE call
    )
{
    VerifyGlobalInit();
    delete call;
}

HC_API void HC_CALLING_CONV
HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call
    )
{
    VerifyGlobalInit();

    HC_HTTP_CALL_PERFORM_FUNC performFunc = get_http_singleton()->m_performFunc;
    if (performFunc != nullptr)
    {
        performFunc(call);
    }
    else
    {
        Internal_HCHttpCallPerform(call);
    }
}


