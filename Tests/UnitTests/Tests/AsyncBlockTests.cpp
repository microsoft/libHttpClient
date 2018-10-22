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

template <typename T>
class AutoRef
{
public:
    AutoRef(T* t) {
        Ref = t;
        Ref->AddRef();
    }
    ~AutoRef() {
        Ref->Release();
    }

    T* Ref;
};

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

    async_queue_handle_t queue = nullptr;

    struct FactorialCallData
    {
        DWORD value = 0;
        DWORD result = 0;
        DWORD iterationWait = 0;
        DWORD workThread = 0;
        std::vector<AsyncOp> opCodes;
        std::atomic<int> inWork = 0;
        std::atomic<int> refs = 1;

        void AddRef() { refs++; }
        void Release() { if (--refs == 0) delete this; }
    };

    static PCWSTR OpName(AsyncOp op)
    {
        switch (op)
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

    static HRESULT CALLBACK FactorialWorkerSimple(AsyncOp opCode, const AsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;

        d->opCodes.push_back(opCode);

        switch (opCode)
        {
        case AsyncOp_Cleanup:
            VERIFY_IS_TRUE(d->inWork == 0);
            d->Release();
            break;

        case AsyncOp_GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case AsyncOp_DoWork:
            d->inWork++;
            d->workThread = GetCurrentThreadId();
            d->result = 1;
            DWORD value = d->value;
            while(value)
            {
                d->result *= value;
                value--;
            }

            if (d->iterationWait != 0)
            {
                Sleep(d->iterationWait);
            }

            d->inWork--;
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
        case AsyncOp_Cleanup:
            VERIFY_IS_TRUE(d->inWork == 0);
            d->Release();
            break;

        case AsyncOp_GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case AsyncOp_DoWork:
            d->inWork++;
            d->workThread = GetCurrentThreadId();
            if (d->result == 0) d->result = 1;
            if (d->value != 0)
            {
                d->result *= d->value;
                d->value--;

                HRESULT hr = ScheduleAsync(data->async, d->iterationWait);
                d->inWork--;

                if (SUCCEEDED(hr))
                {
                    hr = E_PENDING;
                }
                return hr;
            }

            d->inWork--;
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

    static HRESULT FactorialAllocateAsync(DWORD value, AsyncBlock* async)
    {
        void* context;
        HRESULT hr = BeginAsyncAlloc(async, FactorialAsync, __FUNCTION__, FactorialWorkerDistributed, sizeof(FactorialCallData), &context);
        if (SUCCEEDED(hr))
        {
            FactorialCallData* data = new (context) FactorialCallData;
            data->value = value;
            data->AddRef(); // leak a ref on this guy so we don't try to free it.
            hr = ScheduleAsync(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialResult(AsyncBlock* async, _Out_writes_(1) DWORD* result)
    {
        size_t written;
        HRESULT hr = GetAsyncResult(async, FactorialAsync, sizeof(DWORD), result, &written);
        if (SUCCEEDED(hr))
        {
            VERIFY_ARE_EQUAL(sizeof(DWORD), written);
        }
        return hr;
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

    TEST_CLASS_SETUP(TestClassSetup) 
    {
        VERIFY_SUCCEEDED(CreateAsyncQueue(
            AsyncQueueDispatchMode_ThreadPool,
            AsyncQueueDispatchMode_FixedThread,
            &queue));
        UnitTestBase::StartResponseLogging(); 
        return true; 
    }

    TEST_CLASS_CLEANUP(TestClassCleanup) 
    {
        VERIFY_ARE_EQUAL(s_AsyncLibGlobalStateCount, (DWORD)0);
        CloseAsyncQueue(queue);
        UnitTestBase::RemoveResponseLogging();
        return true; 
    }
#endif

    DEFINE_TEST_CASE(VerifySimpleAsyncCall)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
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
        async.queue = queue;

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));
        SleepEx(0, TRUE);

        VERIFY_ARE_EQUAL(data.Ref->result, result);
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);

        ops.push_back(AsyncOp_DoWork);
        ops.push_back(AsyncOp_GetResult);
        ops.push_back(AsyncOp_Cleanup);

        VerifyOps(data.Ref->opCodes, ops);

        VERIFY_ARE_EQUAL(GetCurrentThreadId(), completionThreadId);
    }

    DEFINE_TEST_CASE(VerifyMultipleCalls)
    {
        const DWORD count = 10;
        AsyncBlock async[count];
        FactorialCallData* data[count];
        DWORD completionCount = 0;

        for (int i = 0; i < count; i++)
        {
            data[i] = new FactorialCallData{};
        }

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
            async[idx].queue = queue;
            data[idx]->value = 5 * (idx + 1);

            VERIFY_SUCCEEDED(FactorialAsync(data[idx], &async[idx]));
        }

        UINT64 ticks = GetTickCount64();
        while(completionCount != count && GetTickCount64() - ticks < 5000)
        {
            SleepEx(100, TRUE);
        }

        VERIFY_ARE_EQUAL(count, completionCount);

        // Note: FactorialCallData array elements were cleaned up by FactorialResult.
    }

    DEFINE_TEST_CASE(VerifyDistributedAsyncCall)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
        DWORD result;
        std::vector<AsyncOp> ops;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 100;
        data.Ref->value = 5;

        UINT64 ticks = GetTickCount64();
        VERIFY_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));
        ticks = GetTickCount64() - ticks;
        SleepEx(0, TRUE);

        VERIFY_ARE_EQUAL(data.Ref->result, result);
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);

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

        VerifyOps(data.Ref->opCodes, ops);
    }

    DEFINE_TEST_CASE(VerifyCancellation)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
        HRESULT hrCallback = E_UNEXPECTED;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            hrCallback = GetAsyncStatus(async, false);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 100;
        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));
        Sleep(100);
        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, false), E_PENDING);

        CancelAsync(&async);
        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, true), E_ABORT);
        SleepEx(0, TRUE);
        VERIFY_ARE_EQUAL(E_ABORT, hrCallback);

        VerifyHasOp(data.Ref->opCodes, AsyncOp_Cancel);
        VerifyHasOp(data.Ref->opCodes, AsyncOp_Cleanup);
    }

    DEFINE_TEST_CASE(VerifyCleanupWaitsForWork)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
        DWORD result;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            VERIFY_ARE_EQUAL(FactorialResult(async, &result), E_ABORT);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 500;
        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));

        SleepEx(50, TRUE);
        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, false), E_PENDING);

        CancelAsync(&async);

        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, true), E_ABORT);
        while (SleepEx(700, TRUE) == WAIT_IO_COMPLETION);

        VerifyHasOp(data.Ref->opCodes, AsyncOp_Cancel);
        VerifyHasOp(data.Ref->opCodes, AsyncOp_Cleanup);
        VerifyHasOp(data.Ref->opCodes, AsyncOp_DoWork);
    }

    DEFINE_TEST_CASE(VerifyCleanupWaitsForWorkDistributed)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
        DWORD result;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            VERIFY_ARE_EQUAL(FactorialResult(async, &result), E_ABORT);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 500;
        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));

        SleepEx(700, TRUE);
        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, false), E_PENDING);
        CancelAsync(&async);

        VERIFY_ARE_EQUAL(GetAsyncStatus(&async, true), E_ABORT);
        SleepEx(0, TRUE);

        VerifyHasOp(data.Ref->opCodes, AsyncOp_Cancel);
        VerifyHasOp(data.Ref->opCodes, AsyncOp_Cleanup);
        VerifyHasOp(data.Ref->opCodes, AsyncOp_DoWork);
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
        async.queue = queue;

        expected = 0x12345678;

        VERIFY_SUCCEEDED(RunAsync(&async, WorkThunk::Callback));
        
        result = GetAsyncStatus(&async, true);

        VERIFY_ARE_EQUAL(result, expected);
    }

    DEFINE_TEST_CASE(VerifyCustomQueue)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
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

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));

        VERIFY_IS_TRUE(DispatchAsyncQueue(async.queue, AsyncQueueCallbackType_Work, 100));
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), data.Ref->workThread);

        VERIFY_IS_TRUE(DispatchAsyncQueue(async.queue, AsyncQueueCallbackType_Completion, 100));
        VERIFY_ARE_EQUAL(result, (DWORD)120);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), completionThreadId);

        CloseAsyncQueue(async.queue);
    }

    DEFINE_TEST_CASE(VerifyCantScheduleTwice)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &async.queue));

        VERIFY_SUCCEEDED(BeginAsync(&async, data.Ref, nullptr, nullptr, FactorialWorkerSimple));
        VERIFY_SUCCEEDED(ScheduleAsync(&async, 0));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, ScheduleAsync(&async, 0));

        CancelAsync(&async);
        CloseAsyncQueue(async.queue);
    }

    DEFINE_TEST_CASE(VerifyWaitForCompletion)
    {
        AsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData{});
        DWORD result = 0;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            Sleep(2000);
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));

        VERIFY_ARE_EQUAL(data.Ref->result, result);
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);
    }

    DEFINE_TEST_CASE(VerifyBeginAsyncAlloc)
    {
        AsyncBlock async = {};
        DWORD result;

        CompletionThunk cb([&](AsyncBlock* async)
        {
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        VERIFY_SUCCEEDED(FactorialAllocateAsync(5, &async));
        VERIFY_SUCCEEDED(GetAsyncStatus(&async, true));
        SleepEx(0, TRUE);

        VERIFY_ARE_EQUAL(result, (DWORD)120);
    }

    DEFINE_TEST_CASE(VerifyPeriodicPattern)
    {
        struct Controller
        {
            async_queue_handle_t queue;
            bool enabled;
            uint32_t callbackCount;
            std::function<void(Controller* controller)> schedule;
        };

        auto schedule = [](Controller* controller)
        {
            AsyncBlock *async = new AsyncBlock{};
            async->context = controller;
            async->queue = controller->queue;
            async->callback = [](AsyncBlock* async)
            {
                Controller* pcontroller = (Controller*)async->context;
                pcontroller->callbackCount++;
                if (pcontroller->enabled)
                {
                    pcontroller->schedule(pcontroller);
                }
                delete async;
            };

            VERIFY_SUCCEEDED(BeginAsync(async, controller, nullptr, nullptr, [](AsyncOp op, const AsyncProviderData* data)
            {
                if (op == AsyncOp_DoWork)
                {
                    CompleteAsync(data->async, S_OK, 0);
                }
                return S_OK;
            }));

            VERIFY_SUCCEEDED(ScheduleAsync(async, 30));
        };

        Controller c;
        c.queue = queue;
        c.enabled = true;
        c.callbackCount = 0;
        c.schedule = schedule;

        // Now run this thing for a while
        schedule(&c);

        uint64_t ticks = GetTickCount64();
        while (c.callbackCount < 10)
        {
            SleepEx(100, TRUE);
            VERIFY_IS_TRUE(GetTickCount64() - ticks < 10000);
        }

        c.enabled = false;
        while(SleepEx(500, TRUE) == WAIT_IO_COMPLETION);
    }

    DEFINE_TEST_CASE(VerifyRunAlotAsync)
    {
        int count = 20000;
        WorkThunk cb([&](AsyncBlock*)
        {
            return 0;
        });

        auto asyncs = std::unique_ptr<AsyncBlock[]>(new AsyncBlock[count]{});

        for (int i = 0; i < count; i++)
        {
            auto& async = asyncs[i];
            async.queue = queue;
            async.context = &cb;

            HRESULT hr = RunAsync(&async, WorkThunk::Callback);
            if (FAILED(hr))
            {
                VERIFY_FAIL();
            }
        }

        while (!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work) || 
               !IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion))
        {
            SleepEx(500, TRUE);
        }
    }

    DEFINE_TEST_CASE(VerifyGetAsyncStatusNoDeadlock)
    {
        WorkThunk cb([](AsyncBlock*)
        {
            Sleep(10);
            return 0;
        });

        AsyncBlock async = {};
        async.queue = queue;
        async.context = &cb;

        for (int iteration = 0; iteration < 1000; iteration++)
        {
            VERIFY_SUCCEEDED(RunAsync(&async, WorkThunk::Callback));
            while (GetAsyncStatus(&async, false) == E_PENDING)
            {
                Sleep(0);
            }
        }
    }
};
