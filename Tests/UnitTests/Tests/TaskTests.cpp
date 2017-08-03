// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "singleton.h"

using namespace xbox::httpclient;
bool g_calledTestTaskExecute = false;
bool g_calledTestTaskWriteResults = false;
bool g_calledTestTaskCompleteRoutine = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN


void TestTaskExecute(
    _In_opt_ void* context,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    g_calledTestTaskExecute = true;
    VERIFY_ARE_EQUAL(1, (uint64_t)context);
    HCTaskSetCompleted(taskHandle);
}

void TestTaskWriteResults(
    _In_opt_ void* context,
    _In_ HC_TASK_HANDLE taskHandleId,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext
    )
{
    g_calledTestTaskWriteResults = true;
    VERIFY_ARE_EQUAL(2, (uint64_t)context);

    HC_TASK_EXECUTE_FUNC completeFn = (HC_TASK_EXECUTE_FUNC)completionRoutine;
    if (completeFn != nullptr)
    {
        completeFn(completionRoutineContext, taskHandleId);
    }

}

void TestTaskCompleteRoutine(
    _In_opt_ void* context,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    g_calledTestTaskCompleteRoutine = true;
    VERIFY_ARE_EQUAL(3, (uint64_t)context);
}


DEFINE_TEST_CLASS(TaskTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(TaskTests);

    DEFINE_TEST_CASE(TestHCTaskCreateExecuteNow)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestHCTaskCreateExecuteNow);

        HCGlobalInitialize();
        uint64_t taskGroupId = 1;
        HC_TASK_HANDLE taskHandle = HCTaskCreate(
            taskGroupId,
            TestTaskExecute, (void*)1,
            TestTaskWriteResults, (void*)2,
            TestTaskCompleteRoutine, (void*)3,
            true
            );

        // Verify only execute was called
        VERIFY_ARE_EQUAL(true, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskCompleteRoutine);
        g_calledTestTaskExecute = false;
        g_calledTestTaskWriteResults = false;
        g_calledTestTaskCompleteRoutine = false;
        VERIFY_ARE_EQUAL(false, HCTaskIsTaskPending());

        // Nothing should happen if we do task group 0
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskCompleteRoutine);

        // Verify write & complete were called after 
        HCTaskProcessNextCompletedTask(taskGroupId);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(true, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(true, g_calledTestTaskCompleteRoutine);
        VERIFY_ARE_EQUAL(true, HCTaskIsCompleted(taskHandle));

        VERIFY_IS_NOT_NULL(HCTaskGetPendingHandle());
        VERIFY_IS_NOT_NULL(HCTaskGetCompletedHandle(taskGroupId));

        HCGlobalCleanup();

        //HCTaskWaitForCompleted
    }

    DEFINE_TEST_CASE(TestHCTaskCreateExecuteDelayed)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestHCTaskCreateExecuteDelayed);

        HCGlobalInitialize();
        g_calledTestTaskExecute = false;
        g_calledTestTaskWriteResults = false;
        g_calledTestTaskCompleteRoutine = false;

        uint64_t taskGroupId = 1;
        HC_TASK_HANDLE taskHandle = HCTaskCreate(
            taskGroupId,
            TestTaskExecute, (void*)1,
            TestTaskWriteResults, (void*)2,
            TestTaskCompleteRoutine, (void*)3,
            false
            );

        VERIFY_ARE_EQUAL(true, HCTaskIsTaskPending());
        VERIFY_ARE_EQUAL(false, HCTaskIsCompleted(taskHandle));

        // Verify no called
        VERIFY_ARE_EQUAL(false, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskCompleteRoutine);
        g_calledTestTaskExecute = false;
        g_calledTestTaskWriteResults = false;
        g_calledTestTaskCompleteRoutine = false;

        HCTaskProcessNextPendingTask();

        // Verify execute called
        VERIFY_ARE_EQUAL(true, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskCompleteRoutine);
        g_calledTestTaskExecute = false;
        g_calledTestTaskWriteResults = false;
        g_calledTestTaskCompleteRoutine = false;

        // Nothing should happen if we do task group 0
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskCompleteRoutine);

        // Verify write & complete were called after 
        HCTaskProcessNextCompletedTask(taskGroupId);
        VERIFY_ARE_EQUAL(false, g_calledTestTaskExecute);
        VERIFY_ARE_EQUAL(true, g_calledTestTaskWriteResults);
        VERIFY_ARE_EQUAL(true, g_calledTestTaskCompleteRoutine);
        VERIFY_ARE_EQUAL(true, HCTaskIsCompleted(taskHandle));

        HCTaskWaitForCompleted(taskHandle, 0);

        HCGlobalCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
