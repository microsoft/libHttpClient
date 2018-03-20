// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"
#include "httpClient/async.h"
#include "httpClient/asyncProvider.h"
#include "../mock/mock.h"

using namespace xbox::httpclient;

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallCreate(
    _Out_ HC_CALL_HANDLE* callHandle
    ) HC_NOEXCEPT
try 
{
    if (callHandle == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    HC_CALL* call = new HC_CALL();

    call->retryAllowed = httpSingleton->m_retryAllowed;
    call->timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    call->timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    call->retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;

    call->id = ++httpSingleton->m_lastId;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCreate [ID %llu]", call->id);

    *callHandle = call;
    return HC_OK;
}
CATCH_RETURN()

HC_CALL_HANDLE HCHttpCallDuplicateHandle(
    _In_ HC_CALL_HANDLE call
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallDuplicateHandle [ID %llu]", call->id);
    ++call->refCount;

    return call;
}
CATCH_RETURN_WITH(nullptr)


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallCloseHandle(
    _In_ HC_CALL_HANDLE call
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCloseHandle [ID %llu]", call->id);
    int refCount = --call->refCount;
    if (refCount <= 0)
    {
        assert(refCount == 0); // should only fire at 0
        delete call;
    }

    return HC_OK;
}
CATCH_RETURN()

HRESULT HttpCallPerformDoWork(
    _In_ void* executionRoutineContext,
    _In_ AsyncBlock* asyncBlock
    )
try
{
    auto httpSingleton = get_http_singleton(false);
    if (nullptr == httpSingleton)
        return E_INVALIDARG;

    HC_CALL_HANDLE call = (HC_CALL_HANDLE)executionRoutineContext;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HttpCallPerformDoWork [ID %llu]", call->id);

    bool matchedMocks = false;
    if (httpSingleton->m_mocksEnabled)
    {
        matchedMocks = Mock_Internal_HCHttpCallPerform(call);
        if (matchedMocks)
        {
            CompleteAsync(asyncBlock, S_OK, 0);
        }
    }
   
    if (!matchedMocks) // if there wasn't a matched mock, then real call
    {
        HC_HTTP_CALL_PERFORM_FUNC performFunc = httpSingleton->m_performFunc;
        if (performFunc != nullptr)
        {
            try
            {
                performFunc(call, asyncBlock);
            }
            catch (...)
            {
                HC_TRACE_ERROR(HTTPCLIENT, "HttpCallPerformDoWork [ID %llu]: failed", call->id);
            }
        }
    }

    return E_PENDING;
}
CATCH_RETURN()


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ AsyncBlock* async
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu]", call->id);
    call->performCalled = true;

    HC_RESULT hr = HRESULTtoHC(BeginAsync(async, call, HCHttpCallPerform, __FUNCTION__, 
        [](_In_ AsyncOp op, _Inout_ AsyncProviderData* data)
    {
        switch (op)
        {
            case AsyncOp_DoWork: return HttpCallPerformDoWork(data->context, data->async);
        }

        return S_OK;
    }));

    if (hr == HC_OK)
    {
        hr = HRESULTtoHC(ScheduleAsync(async, 0));
    }

    return hr;
}
CATCH_RETURN()

