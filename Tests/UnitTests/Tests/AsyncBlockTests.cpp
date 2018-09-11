// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "async.h"
#include "asyncprovider.h"
#include "asyncqueue.h"

extern std::atomic<uint32_t> s_AsyncLibGlobalStateCount;

class CompletionThunk
{
public:
    CompletionThunk(std::function<void(AsyncBlock*)> func)
        : _func(func)
    {
    }

    static void CALLBACK Callback(AsyncBlock* async)
    {
        const CompletionThunk* pthis = static_cast<CompletionThunk*>(async->context);
        pthis->_func(async);
    }

private:

    std::function<void(AsyncBlock*)> _func;
};

class WorkThunk
{
public:
    WorkThunk(std::function<HRESULT(AsyncBlock*)> func)
        : _func(func)
    {
    }

    static HRESULT CALLBACK Callback(AsyncBlock* async)
    {
        const WorkThunk* pthis = static_cast<WorkThunk*>(async->context);
        return pthis->_func(async);
    }

private:

    std::function<HRESULT(AsyncBlock*)> _func;
};

DEFINE_TEST_CLASS(AsyncBlockTests)
{
private:

    struct FactorialCallData
    {
        DWORD value = 0;
        DWORD result = 0;
        DWORD iterationWait = 0;
        DWORD workThread = 0;
        std::vector<AsyncOp> opCodes;
    };

    static HRESULT CALLBACK FactorialWorkerSimple(AsyncOp opCode, const AsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;

        d->opCodes.push_back(opCode);

        VERIFY_IS_NOT_NULL(data->queue);

        switch (opCode)
        {
        case AsyncOp_GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case AsyncOp_DoWork:
            d->workThread = GetCurrentThreadId();
            d->result = 1;
            DWORD value = d->value;
            while(value)
            {
                d->result *= value;
                value--;
            }

            CompleteAsync(data->async, S_OK, sizeof(DWORD));
            break;
        }

        return S_OK;
    }

    static HRESULT CALLBACK FactorialWorkerDistributed(AsyncOp opCode, const AsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;

        d->opCodes.push_back(opCode);

        switch (opCode)
        {
        case AsyncOp_GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case AsyncOp_DoWork:
            d->workThread = GetCurrentThreadId();
            if (d->result == 0) d->result = 1;
            if (d->value != 0)
            {
                d->result *= d->value;
                d->value--;

                VERIFY_SUCCEEDED(ScheduleAsync(data->async, d->iterationWait));
                return E_PENDING;
            }

            CompleteAsync(data->async, S_OK, sizeof(DWORD));
            break;
        }

        return S_OK;
    }

    static HRESULT FactorialAsync(FactorialCallData* data, AsyncBlock* async)
    {
        HRESULT hr = BeginAsync(async, data, FactorialAsync, __FUNCTION__, FactorialWorkerSimple);
        if (SUCCEEDED(hr))
        {
            hr = ScheduleAsync(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialDistributedAsync(FactorialCallData* data, AsyncBlock* async)
    {
        HRESULT hr = BeginAsync(async, data, FactorialAsync, __FUNCTION__, FactorialWorkerDistributed);
        if (SUCCEEDED(hr))
        {
            hr = ScheduleAsync(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialResult(AsyncBlock* async, _Out_writes_(1) DWORD* result)
    {
        size_t written;
        HRESULT hr = GetAsyncResult(async, FactorialAsync, sizeof(DWORD), result, &written);
        VERIFY_ARE_EQUAL(sizeof(DWORD), written);
        return hr;
    }

    static PCWSTR OpName(AsyncOp op)
    {
        switch(op)
        {
            case AsyncOp_GetResult:
                return L"GetResult";

            case AsyncOp_Cleanup:
                return L"Cleanup";
            
            case AsyncOp_DoWork:
                return L"DoWork";

            case AsyncOp_Cancel:
                return L"Cancel";

            default:
                VERIFY_FAIL();
                return L"Unknown";
        }
    }

    static void VerifyOps(const std::vector<AsyncOp>& opsActual, const std::vector<AsyncOp>& opsExpected)
    {
        size_t size = opsActual.size();
        VERIFY_ARE_EQUAL(size, opsExpected.size());

        for(size_t i = 0; i < size; i++)
        {
            VERIFY_ARE_EQUAL(opsActual[i], opsExpected[i]);
        }
    }

    static void VerifyHasOp(const std::vector<AsyncOp>& opsActual, AsyncOp expected)
    {
        bool found = false;
        for(auto op : opsActual)
        {
            if (op == expected)
            {
                found = true;
                break;
            }
        }

        VERIFY_IS_TRUE(found);
    }

public:

#ifdef USING_TAEF
    TEST_CLASS(AsyncBlockTests)

    TEST_CLASS_SETUP(TestClassSetup) { UnitTestBase::StartResponseLogging(); return true; }

    TEST_CLASS_CLEANUP(TestClassCleanup) 
    {
        VERIFY_ARE_EQUAL(s_AsyncLibGlobalStateCount, (DWORD)0);
        UnitTestBase::RemoveResponseLogging(); 
        return true; 
    }
#endif

    DEFINE_TEST_CASE(VerifySimpleAsyncCall)
    {
        AsyncBlock async = {};
        FactorialCallData data = {};
        DWORD result;
        DWORD completionThreadId;
        std::vector<AsyncOp> ops;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            completionThreadId = GetCurrentThreadId();
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;

        data.value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(&data, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));
        SleepEx(0, TRUE);

        VERIFY_ARE_EQUAL(data.result, result);
        VERIFY_ARE_EQUAL(data.result, (DWORD)120);

        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_GetResult);
        ops.push_back(AsyncOp_Cleanup);

        VerifyOps(data.opCodes, ops);

        VERIFY_ARE_EQUAL(GetCurrentThreadId(), completionThreadId);
    }

    DEFINE_TEST_CASE(VerifyMultipleCalls)
    {
        const DWORD count = 10;
        AsyncBlock async[count];
        FactorialCallData data[count];
        DWORD completionCount = 0;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            DWORD result;
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
            InterlockedIncrement(&completionCount);
        });

        ZeroMemory(async, sizeof(async));

        for (int idx = 0; idx < count; idx++)
        {
            async[idx].context = &cb;
            async[idx].callback = CompletionThunk::Callback;
            data[idx].value = 5 * (idx + 1);

            VERIFY_SUCCEEDED(FactorialAsync(&data[idx], &async[idx]));
        }

        UINT64 ticks = GetTickCount64();
        while(completionCount != count && GetTickCount64() - ticks < 5000)
        {
            SleepEx(100, TRUE);
        }

        VERIFY_ARE_EQUAL(count, completionCount);
    }

    DEFINE_TEST_CASE(VerifyDistributedAsyncCall)
    {
        AsyncBlock async = {};
        FactorialCallData data = {};
        DWORD result;
        std::vector<AsyncOp> ops;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;

        data.iterationWait = 100;
        data.value = 5;

        UINT64 ticks = GetTickCount64();
        VERIFY_SUCCEEDED(FactorialDistributedAsync(&data, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));
        ticks = GetTickCount64() - ticks;
        SleepEx(0, TRUE);

        VERIFY_ARE_EQUAL(data.result, result);
        VERIFY_ARE_EQUAL(data.result, (DWORD)120);

        // Iteration wait should have paused 100ms between each iteration.
        VERIFY_IS_TRUE(ticks >= (UINT64)500);

        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_GetResult);
        ops.push_back(AsyncOp_Cleanup);

        VerifyOps(data.opCodes, ops);
    }

    DEFINE_TEST_CASE(VerifyCancellation)
    {
        AsyncBlock async = {};
        FactorialCallData data = {};
        HRESULT hrCallback = E_UNEXPECTED;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            hrCallback = GetAsyncStatus(async, false);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;

        data.iterationWait = 100;
        data.value = 5;

        VERIFY_SUCCEEDED(FactorialDistributedAsync(&data, &async));
        CancelAsync(&async);
        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, true), E_ABORT);
        SleepEx(0, TRUE);
        VERIFY_ARE_EQUAL(E_ABORT, hrCallback);

        VerifyHasOp(data.opCodes, AsyncOp_Cancel);
        VerifyHasOp(data.opCodes, AsyncOp_Cleanup);
    }

    DEFINE_TEST_CASE(VerifyRunAsync)
    {
        AsyncBlock async = {};
        HRESULT expected, result;

        WorkThunk cb([&](AsyncBlock*)
        {
            return expected;
        });

        async.context = &cb;

        expected = 0x12345678;

        VERIFY_SUCCEEDED(RunAsync(&async, WorkThunk::Callback));
        
        result = GetAsyncStatus(&async, true);

        VERIFY_ARE_EQUAL(result, expected);
    }

    DEFINE_TEST_CASE(VerifyCustomQueue)
    {
        AsyncBlock async = {};
        FactorialCallData data = {};
        DWORD result;
        DWORD completionThreadId;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            completionThreadId = GetCurrentThreadId();
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &async.queue));

        data.value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(&data, &async));

        VERIFY_IS_TRUE(DispatchAsyncQueue(async.queue, AsyncQueueCallbackType_Work, 100));
        VERIFY_ARE_EQUAL(data.result, (DWORD)120);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), data.workThread);
        
        VERIFY_IS_TRUE(DispatchAsyncQueue(async.queue, AsyncQueueCallbackType_Completion, 100));
        VERIFY_ARE_EQUAL(result, (DWORD)120);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), completionThreadId);

        CloseAsyncQueue(async.queue);
    }

    DEFINE_TEST_CASE(VerifyCantScheduleTwice)
    {
        AsyncBlock async = {};
        FactorialCallData data = {};

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &async.queue));

        VERIFY_SUCCEEDED(BeginAsync(&async, &data, nullptr, nullptr, FactorialWorkerSimple));
        VERIFY_SUCCEEDED(ScheduleAsync(&async, 0));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, ScheduleAsync(&async, 0));

        CancelAsync(&async);
        CloseAsyncQueue(async.queue);
    }

    DEFINE_TEST_CASE(VerifyWaitForCompletion)
    {
        AsyncBlock async = {};
        FactorialCallData data = {};
        DWORD result = 0;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            Sleep(2000);
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;

        data.value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(&data, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));

        VERIFY_ARE_EQUAL(data.result, result);
        VERIFY_ARE_EQUAL(data.result, (DWORD)120);
    }
};
