// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"
#include "Mock/http_mock.h"

using namespace xbox::httpclient;

HC_API void HC_CALLING_CONV
HCHttpCallCreate(
    _Out_ HC_CALL_HANDLE* callHandle
    )
{
    auto httpSingleton = get_http_singleton();

    HC_CALL* call = new HC_CALL();

    call->retryAllowed = httpSingleton->m_retryAllowed;
    call->timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    call->timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    call->retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
    call->enableAssertsForThrottling = httpSingleton->m_enableAssertsForThrottling;

    call->id = httpSingleton->m_lastId.load();
    httpSingleton->m_lastId++;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCreate [ID %llu]", call->id);

    *callHandle = call;
}

HC_API void HC_CALLING_CONV
HCHttpCallCleanup(
    _In_ HC_CALL_HANDLE call
    )
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCleanup [ID %llu]", call->id);
    delete call;
}

void HttpCallPerformExecute(
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    auto httpSingleton = get_http_singleton();

    HC_CALL_HANDLE call = (HC_CALL_HANDLE)executionRoutineContext;
    if (call == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerformExecute null call");
        return;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu]", call->id);

    bool matchedMocks = false;
    if (httpSingleton->m_mocksEnabled)
    {
        matchedMocks = Mock_Internal_HCHttpCallPerform(call);
        if (matchedMocks)
        {
            HCTaskSetCompleted(taskHandle);
        }
    }
   
    if (!matchedMocks) // if there wasn't a matched mock, then real call
    {
        HC_HTTP_CALL_PERFORM_FUNC performFunc = httpSingleton->m_performFunc;
        if (performFunc != nullptr)
        {
            try
            {
                performFunc(call, taskHandle);
            }
            catch (...)
            {
                HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu]: failed", call->id);
            }
        }
    }
}

void HttpCallPerformWriteResults(
    _In_opt_ void* writeResultsRoutineContext,
    _In_ HC_TASK_HANDLE taskHandleId,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext
    )
{
    HC_CALL_HANDLE call = (HC_CALL_HANDLE)writeResultsRoutineContext;

    if (call != nullptr)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformWriteResults [ID %llu]", call->id);

        HCHttpCallPerformCompletionRoutine completeFn = (HCHttpCallPerformCompletionRoutine)completionRoutine;
        if (completeFn != nullptr)
        {
            completeFn(completionRoutineContext, call);
        }
    }
}

HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCHttpCallPerform(
    _In_ uint64_t taskGroupId,
    _In_ HC_CALL_HANDLE call,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCHttpCallPerformCompletionRoutine completionRoutine
    )
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu]", call->id);

    return HCTaskCreate(
        taskGroupId,
        HttpCallPerformExecute, (void*)call,
        HttpCallPerformWriteResults, (void*)call,
        completionRoutine, completionRoutineContext
        );
}


