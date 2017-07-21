// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"
#include "Mock/http_mock.h"

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

void HttpCallPerformExecute(
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    HC_CALL_HANDLE call = (HC_CALL_HANDLE)executionRoutineContext;

    bool matchedMocks = false;
    if (get_http_singleton()->m_mocksEnabled)
    {
        matchedMocks = Mock_Internal_HCHttpCallPerform(call);
        if (matchedMocks)
        {
            HCTaskSetResultReady(taskHandle);
        }
    }
   
    if (!matchedMocks) // if there wasn't a matched mock, then real call
    {
        HC_HTTP_CALL_PERFORM_FUNC performFunc = get_http_singleton()->m_performFunc;
        if (performFunc != nullptr)
        {
            try
            {
                performFunc(call, taskHandle);
            }
            catch (...)
            {
                LOG_ERROR("HCHttpCallPerform failed");
            }
        }
    }
}

void HttpCallPerformWriteResults(
    _In_opt_ void* writeResultsRoutineContext,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    HC_CALL_HANDLE call = (HC_CALL_HANDLE)writeResultsRoutineContext;
    HCHttpCallPerformCompletionRoutine completeFn = (HCHttpCallPerformCompletionRoutine)taskHandle->completionRoutine;
    if (completeFn != nullptr)
    {
        completeFn(taskHandle->completionRoutineContext, call);
    }
}

HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCHttpCallPerform(
    _In_ uint32_t taskGroupId,
    _In_ HC_CALL_HANDLE call,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCHttpCallPerformCompletionRoutine completionRoutine
    )
{
    VerifyGlobalInit();

    return HCTaskCreate(
        taskGroupId,
        HttpCallPerformExecute, (void*)call,
        HttpCallPerformWriteResults, (void*)call,
        completionRoutine, completionRoutineContext,
        true
        );
}


