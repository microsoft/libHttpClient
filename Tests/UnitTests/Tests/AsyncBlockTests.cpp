// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "XAsync.h"
#include "XAsyncProvider.h"
#include "XTaskQueue.h"
#include "XTaskQueuePriv.h"

#define TEST_CLASS_OWNER L"brianpe"

extern std::atomic<uint32_t> s_AsyncLibGlobalStateCount;

#define VERIFY_QUEUE_EMPTY(q) { VERIFY_IS_TRUE(XTaskQueueIsEmpty(q, XTaskQueuePort::Completion)); VERIFY_IS_TRUE(XTaskQueueIsEmpty(q, XTaskQueuePort::Work)); }

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
    CompletionThunk(std::function<void(XAsyncBlock*)> func)
        : _func(func)
    {
    }

    static void CALLBACK Callback(XAsyncBlock* async)
    {
        const CompletionThunk* pthis = static_cast<CompletionThunk*>(async->context);
        pthis->_func(async);
    }

private:

    std::function<void(XAsyncBlock*)> _func;
};

class WorkThunk
{
public:
    WorkThunk(std::function<HRESULT(XAsyncBlock*)> func)
        : _func(func)
    {
    }

    static HRESULT CALLBACK Callback(XAsyncBlock* async)
    {
        const WorkThunk* pthis = static_cast<WorkThunk*>(async->context);
        return pthis->_func(async);
    }

private:

    std::function<HRESULT(XAsyncBlock*)> _func;
};

DEFINE_TEST_CLASS(AsyncBlockTests)
{
private:

    XTaskQueueHandle queue = nullptr;

    struct FactorialCallData
    {
        DWORD value = 0;
        DWORD result = 0;
        DWORD iterationWait = 0;
        DWORD workThread = 0;
        std::vector<XAsyncOp> opCodes;
        std::atomic<int> inWork = 0;
        std::atomic<int> refs = 1;

        void AddRef() { refs++; }
        void Release() { if (--refs == 0) delete this; }
    };

    static PCWSTR OpName(XAsyncOp op)
    {
        switch(op)
        {
            case XAsyncOp::Begin:
                return L"Begin";

            case XAsyncOp::GetResult:
                return L"GetResult";

            case XAsyncOp::Cleanup:
                return L"Cleanup";
            
            case XAsyncOp::DoWork:
                return L"DoWork";

            case XAsyncOp::Cancel:
                return L"Cancel";

            default:
                VERIFY_FAIL();
                return L"Unknown";
        }
    }

    static HRESULT CALLBACK FactorialWorkerSimple(XAsyncOp opCode, const XAsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;

        d->opCodes.push_back(opCode);

        switch (opCode)
        {
        case XAsyncOp::Cleanup:
            VERIFY_IS_TRUE(d->inWork == 0);
            d->Release();
            break;

        case XAsyncOp::GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case XAsyncOp::DoWork:
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
            XAsyncComplete(data->async, S_OK, sizeof(DWORD));
            break;
        }

        return S_OK;
    }

    static HRESULT CALLBACK FactorialWorkerDistributed(XAsyncOp opCode, const XAsyncProviderData* data)
    {
        FactorialCallData* d = (FactorialCallData*)data->context;

        d->opCodes.push_back(opCode);

        switch (opCode)
        {
        case XAsyncOp::Cleanup:
            VERIFY_IS_TRUE(d->inWork == 0);
            d->Release();
            break;

        case XAsyncOp::GetResult:
            CopyMemory(data->buffer, &d->result, sizeof(DWORD));
            break;

        case XAsyncOp::DoWork:
            d->inWork++;
            d->workThread = GetCurrentThreadId();
            if (d->result == 0) d->result = 1;
            if (d->value != 0)
            {
                d->result *= d->value;
                d->value--;

                HRESULT hr = XAsyncSchedule(data->async, d->iterationWait);
                d->inWork--;

                if (SUCCEEDED(hr))
                {
                    hr = E_PENDING;
                }
                return hr;
            }

            d->inWork--;
            XAsyncComplete(data->async, S_OK, sizeof(DWORD));
            break;
        }

        return S_OK;
    }

    static HRESULT FactorialAsync(FactorialCallData* data, XAsyncBlock* async)
    {
        HRESULT hr = XAsyncBegin(async, data, FactorialAsync, __FUNCTION__, FactorialWorkerSimple);
        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialDistributedAsync(FactorialCallData* data, XAsyncBlock* async)
    {
        HRESULT hr = XAsyncBegin(async, data, FactorialAsync, __FUNCTION__, FactorialWorkerDistributed);
        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialAllocateAsync(DWORD value, XAsyncBlock* async)
    {
        void* context;
        HRESULT hr = XAsyncBeginAlloc(async, FactorialAsync, __FUNCTION__, FactorialWorkerDistributed, sizeof(FactorialCallData), &context);
        if (SUCCEEDED(hr))
        {
            FactorialCallData* data = new (context) FactorialCallData;
            data->value = value;
            data->AddRef(); // leak a ref on this guy so we don't try to free it.
            hr = XAsyncSchedule(async, 0);
        }
        return hr;
    }

    static HRESULT FactorialResult(XAsyncBlock* async, _Out_writes_(1) DWORD* result)
    {
        size_t written;
        HRESULT hr = XAsyncGetResult(async, FactorialAsync, sizeof(DWORD), result, &written);
        if (SUCCEEDED(hr))
        {
            VERIFY_ARE_EQUAL(sizeof(DWORD), written);
        }
        return hr;
    }

    static void VerifyOps(const std::vector<XAsyncOp>& opsActual, const std::vector<XAsyncOp>& opsExpected)
    {
        LOG_COMMENT(L"Actual:");
        for(auto op : opsActual)
        {
            LOG_COMMENT(L"  %ws", OpName(op));
        }

        LOG_COMMENT(L"Expected:");
        for(auto op : opsExpected)
        {
            LOG_COMMENT(L"  %ws", OpName(op));
        }

        size_t size = opsActual.size();
        VERIFY_ARE_EQUAL(size, opsExpected.size());

        for(size_t i = 0; i < size; i++)
        {
            VERIFY_ARE_EQUAL(opsActual[i], opsExpected[i]);
        }
    }

    static void VerifyHasOp(const std::vector<XAsyncOp>& opsActual, XAsyncOp expected)
    {
        LOG_COMMENT(L"Actual:");
        for(auto op : opsActual)
        {
            LOG_COMMENT(L"  %ws", OpName(op));
        }

        bool found = false;

        LOG_COMMENT(L"Expected: %ws", OpName(expected));
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

    BEGIN_TEST_CLASS(AsyncBlockTests)
    END_TEST_CLASS()


#else
    DEFINE_TEST_CLASS_PROPS(AsyncBlockTests);

#endif

    AsyncBlockTests::AsyncBlockTests()
    {
        VERIFY_SUCCEEDED(XTaskQueueCreate(
            XTaskQueueDispatchMode::ThreadPool,
            XTaskQueueDispatchMode::ThreadPool,
            &queue));
    }

    AsyncBlockTests::~AsyncBlockTests()
    {
        VERIFY_ARE_EQUAL(s_AsyncLibGlobalStateCount, (DWORD)0);
        XTaskQueueCloseHandle(queue);
    }

    DEFINE_TEST_CASE(VerifySimpleAsyncCall)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;
        std::vector<XAsyncOp> ops;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));

        VERIFY_ARE_EQUAL(data.Ref->result, result);
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);

        ops.push_back(XAsyncOp::Begin);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::GetResult);
        ops.push_back(XAsyncOp::Cleanup);

        VerifyOps(data.Ref->opCodes, ops);

        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyMultipleCalls)
    {
        const DWORD count = 10;
        XAsyncBlock async[count];
        FactorialCallData* data[count];
        DWORD completionCount = 0;

        for (int i = 0; i < count; i++)
        {
            data[i] = new FactorialCallData{};
        }

        CompletionThunk cb([&](XAsyncBlock* async)
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
            while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 100)) { }
        }

        VERIFY_ARE_EQUAL(count, completionCount);

        // Note: FactorialCallData array elements were cleaned up by FactorialResult.
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyDistributedAsyncCall)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;
        std::vector<XAsyncOp> ops;

        CompletionThunk cb([&](XAsyncBlock* async)
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
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));
        ticks = GetTickCount64() - ticks;
        XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);

        VERIFY_ARE_EQUAL(data.Ref->result, result);
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);

        // Iteration wait should have paused 100ms between each iteration.
        VERIFY_IS_GREATER_THAN_OR_EQUAL(ticks, (UINT64)500);

        ops.push_back(XAsyncOp::Begin);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::DoWork);
        ops.push_back(XAsyncOp::GetResult);
        ops.push_back(XAsyncOp::Cleanup);

        VerifyOps(data.Ref->opCodes, ops);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyCancellation)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        HRESULT hrCallback = E_UNEXPECTED;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            hrCallback = XAsyncGetStatus(async, false);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 100;
        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));
        Sleep(100);
        VERIFY_ARE_EQUAL(XAsyncGetStatus(&async, false), E_PENDING);

        XAsyncCancel(&async);
        VERIFY_ARE_EQUAL(XAsyncGetStatus(&async, true), E_ABORT);
        Sleep(500);
        VERIFY_ARE_EQUAL(E_ABORT, hrCallback);

        VerifyHasOp(data.Ref->opCodes, XAsyncOp::Cancel);
        VerifyHasOp(data.Ref->opCodes, XAsyncOp::Cleanup);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyCleanupWaitsForWork)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            VERIFY_ARE_EQUAL(FactorialResult(async, &result), E_ABORT);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 500;
        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));

        while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 50)) { }
        VERIFY_ARE_EQUAL(XAsyncGetStatus(&async, false), E_PENDING);

        XAsyncCancel(&async);

        VERIFY_ARE_EQUAL(XAsyncGetStatus(&async, true), E_ABORT);
        XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 700);

        VerifyHasOp(data.Ref->opCodes, XAsyncOp::Cancel);
        VerifyHasOp(data.Ref->opCodes, XAsyncOp::Cleanup);
        VerifyHasOp(data.Ref->opCodes, XAsyncOp::DoWork);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyCleanupWaitsForWorkDistributed)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            VERIFY_ARE_EQUAL(FactorialResult(async, &result), E_ABORT);
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->iterationWait = 500;
        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialDistributedAsync(data.Ref, &async));

        while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 700)) { }
        VERIFY_ARE_EQUAL(XAsyncGetStatus(&async, false), E_PENDING);
        XAsyncCancel(&async);

        VERIFY_ARE_EQUAL(XAsyncGetStatus(&async, true), E_ABORT);
        Sleep(500);

        VerifyHasOp(data.Ref->opCodes, XAsyncOp::Cancel);
        VerifyHasOp(data.Ref->opCodes, XAsyncOp::Cleanup);
        VerifyHasOp(data.Ref->opCodes, XAsyncOp::DoWork);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyRunAsync)
    {
        XAsyncBlock async = {};
        HRESULT expected, result;

        WorkThunk cb([&](XAsyncBlock*)
        {
            return expected;
        });

        async.context = &cb;
        async.queue = queue;

        expected = 0x12345678;

        VERIFY_SUCCEEDED(XAsyncRun(&async, WorkThunk::Callback));
        
        result = XAsyncGetStatus(&async, true);
        Sleep(500);

        VERIFY_ARE_EQUAL(result, expected);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyCustomQueue)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;
        DWORD completionThreadId;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            completionThreadId = GetCurrentThreadId();
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &async.queue));

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));

        VERIFY_IS_TRUE(XTaskQueueDispatch(async.queue, XTaskQueuePort::Work, 100));
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), data.Ref->workThread);
        
        VERIFY_IS_TRUE(XTaskQueueDispatch(async.queue, XTaskQueuePort::Completion, 100));
        VERIFY_ARE_EQUAL(result, (DWORD)120);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), completionThreadId);

        VERIFY_QUEUE_EMPTY(async.queue);
        XTaskQueueCloseHandle(async.queue);
    }

    DEFINE_TEST_CASE(VerifyCantScheduleTwice)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &async.queue));

        VERIFY_SUCCEEDED(XAsyncBegin(&async, data.Ref, nullptr, nullptr, FactorialWorkerSimple));
        VERIFY_SUCCEEDED(XAsyncSchedule(&async, 0));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, XAsyncSchedule(&async, 0));

        XAsyncCancel(&async);

        // Dispatch to clear out the queue
        while(XTaskQueueDispatch(async.queue, XTaskQueuePort::Work, 0));

        VERIFY_QUEUE_EMPTY(async.queue);
        XTaskQueueCloseHandle(async.queue);
    }

    DEFINE_TEST_CASE(VerifyWaitForCompletion)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result = 0;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            Sleep(2000);
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));

        VERIFY_ARE_EQUAL(data.Ref->result, result);
        VERIFY_ARE_EQUAL(data.Ref->result, (DWORD)120);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyBeginAsyncAlloc)
    {
        XAsyncBlock async = {};
        DWORD result;

        CompletionThunk cb([&](XAsyncBlock* async)
        {
            VERIFY_SUCCEEDED(FactorialResult(async, &result));
        });

        async.context = &cb;
        async.callback = CompletionThunk::Callback;
        async.queue = queue;

        VERIFY_SUCCEEDED(FactorialAllocateAsync(5, &async));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));
        Sleep(500);

        VERIFY_ARE_EQUAL(result, (DWORD)120);
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyPeriodicPattern)
    {
        struct Controller
        {
            XTaskQueueHandle queue;
            bool enabled;
            uint32_t callbackCount;
            std::function<void(Controller* controller)> schedule;
        };

        auto schedule = [](Controller* controller)
        {
            XAsyncBlock *async = new XAsyncBlock{};
            async->context = controller;
            async->queue = controller->queue;
            async->callback = [](XAsyncBlock* async)
            {
                Controller* pcontroller = (Controller*)async->context;
                pcontroller->callbackCount++;
                if (pcontroller->enabled)
                {
                    pcontroller->schedule(pcontroller);
                }
                delete async;
            };

            VERIFY_SUCCEEDED(XAsyncBegin(async, controller, nullptr, nullptr, [](XAsyncOp op, const XAsyncProviderData* data)
            {
                if (op == XAsyncOp::DoWork)
                {
                    XAsyncComplete(data->async, S_OK, 0);
                }
                return S_OK;
            }));

            VERIFY_SUCCEEDED(XAsyncSchedule(async, 30));
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
            Sleep(100);
            VERIFY_IS_TRUE(GetTickCount64() - ticks < 10000);
        }

        c.enabled = false;
        while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 500)) { }
        VERIFY_QUEUE_EMPTY(queue);
    }

    DEFINE_TEST_CASE(VerifyRunAlotAsync)
    {
        int count = 20000;
        WorkThunk cb([&](XAsyncBlock*)
        {
            return 0;
        });

        auto asyncs = std::unique_ptr<XAsyncBlock[]>(new XAsyncBlock[count]{});

        for (int i = 0; i < count; i++)
        {
            auto& async = asyncs[i];
            async.queue = queue;
            async.context = &cb;

            HRESULT hr = XAsyncRun(&async, WorkThunk::Callback);
            if (FAILED(hr))
            {
                VERIFY_FAIL();
            }
        }

        while (!XTaskQueueIsEmpty(queue, XTaskQueuePort::Work) || 
               !XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion))
        {
            while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 500)) { }
        }
    }

    DEFINE_TEST_CASE(VerifyGetAsyncStatusNoDeadlock)
    {
        WorkThunk cb([](XAsyncBlock*)
        {
            Sleep(10);
            return 0;
        });

        XAsyncBlock async = {};
        async.queue = queue;
        async.context = &cb;

        for (int iteration = 0; iteration < 500; iteration++)
        {
            HRESULT hr = XAsyncRun(&async, WorkThunk::Callback);
            if (FAILED(hr)) VERIFY_SUCCEEDED(hr);
            while (XAsyncGetStatus(&async, false) == E_PENDING)
            {
                Sleep(0);
            }
        }
    }

    DEFINE_TEST_CASE(VerifyGlobalQueueUsage)
    {
        XAsyncBlock async = { };

        auto nopProvider = [](XAsyncOp, const XAsyncProviderData*)
        {
            return S_OK;
        };

        // Verify we use the global queue
        VERIFY_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, nopProvider));
        XAsyncCancel(&async);

        // Now null the global queue and verify the right error happens
        XTaskQueueHandle globalQueue;
        VERIFY_IS_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&globalQueue));
        XTaskQueueSetCurrentProcessTaskQueue(nullptr);

        VERIFY_ARE_EQUAL(E_NO_TASK_QUEUE, XAsyncBegin(&async, nullptr, nullptr, nullptr, nopProvider));
        XTaskQueueSetCurrentProcessTaskQueue(globalQueue);
        XTaskQueueCloseHandle(globalQueue);
    }

    DEFINE_TEST_CASE(VerifyFailedBeginCompletes)
    {
        XAsyncBlock async{};
        async.queue = queue;

        auto failProvider = [](XAsyncOp op, const XAsyncProviderData*)
        {
            return op == XAsyncOp::Begin ? E_FAIL : E_UNEXPECTED;
        };

        // XAsyncBegin should still succeed even if the begin op fails, because
        // the call was successfully started.
        VERIFY_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, failProvider));
        VERIFY_ARE_EQUAL(E_FAIL, XAsyncGetStatus(&async, true));
    }
};
