// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "XAsync.h"
#include "XAsyncProvider.h"
#include "XAsyncProviderPriv.h"
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
        std::atomic<int> refs = 0;
        std::atomic<bool> canceled = false;

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
        case XAsyncOp::Begin:
            d->AddRef();
            break;

        case XAsyncOp::Cancel:
            d->canceled = true;
            break;

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
            XAsyncComplete(data->async, d->canceled ? E_ABORT : S_OK, sizeof(DWORD));
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
        case XAsyncOp::Begin:
            d->AddRef();
            break;

        case XAsyncOp::Cancel:
            d->canceled = true;
            break;

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
                if (d->canceled)
                {
                    d->inWork--;
                    return E_ABORT;
                }

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

    static HRESULT CALLBACK FactorialWorkerDistributedWithSchedule(XAsyncOp opCode, const XAsyncProviderData* data)
    {
        if (opCode == XAsyncOp::Begin)
        {
            FactorialCallData* d = (FactorialCallData*)data->context;

            // leak a ref on this guy so we don't try to free it. We need
            // to do two addrefs because a new object starts with refcount
            // of zero.  The factorial async process will addref/release so
            // we need two to "leak" it (not really leaked; the memory is
            // owned by the async logic)

            d->AddRef();
            d->AddRef();
        }

        HRESULT hr =  FactorialWorkerDistributed(opCode, data);

        if (SUCCEEDED(hr) && opCode == XAsyncOp::Begin)
        {
            hr = XAsyncSchedule(data->async, 0);
        }

        return hr;
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
        HRESULT hr = XAsyncBeginAlloc(async, FactorialAsync, __FUNCTION__, FactorialWorkerDistributedWithSchedule, sizeof(FactorialCallData), sizeof(DWORD), &value);
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
        XTaskQueueTerminate(queue, true, nullptr, nullptr);
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

    DEFINE_TEST_CASE(VerifyAsyncBlockReuse)
    {
        // Specifically allow stack garbage here.
        XAsyncBlock async;

        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});

        DWORD result;

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

        VERIFY_ARE_EQUAL(result, (DWORD)120);

        // Now reuse the async block -- that should be fine.
        // Don't configure a callback so we can leave the
        // block open.

        async.callback = nullptr;
        data.Ref->value = 6;
        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));

        // It shoould NOT be fine to try to reuse it again before
        // we've pulled results.

        VERIFY_ARE_EQUAL(E_INVALIDARG, FactorialAsync(data.Ref, &async));

        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));
        VERIFY_SUCCEEDED(FactorialResult(&async, &result));

        VERIFY_ARE_EQUAL(result, (DWORD)720);
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

        auto nopProvider = [](XAsyncOp op, const XAsyncProviderData* d)
        {
            if (op == XAsyncOp::Cancel)
            {
                XAsyncComplete(d->async, E_ABORT, 0);
            }
            return S_OK;
        };

        // Verify we use the global queue
        VERIFY_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, nopProvider));
        XAsyncCancel(&async);
        VERIFY_ARE_EQUAL(E_ABORT, XAsyncGetStatus(&async, true));

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

    DEFINE_TEST_CASE(VerifyZeroPayloadCleansUpFast)
    {
        XAsyncBlock async{};

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &async.queue));

        struct Context
        {
            uint32_t cleanupCount = 0;
        };

        auto emptyProvider = [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
                VERIFY_SUCCEEDED(XAsyncSchedule(data->async, 0));
                break;

            case XAsyncOp::DoWork:
                XAsyncComplete(data->async, S_OK, 0);
                break;

            case XAsyncOp::Cleanup:
                ((Context*)data->context)->cleanupCount++;
                break;
            }

            return S_OK;
        };

        Context cxt;
        cxt.cleanupCount = 0;

        VERIFY_SUCCEEDED(XAsyncBegin(&async, &cxt, nullptr, nullptr, emptyProvider));
        VERIFY_IS_TRUE(XTaskQueueDispatch(async.queue, XTaskQueuePort::Work, 0));
        VERIFY_ARE_EQUAL((uint32_t)1, cxt.cleanupCount);
        
        while(XTaskQueueDispatch(async.queue, XTaskQueuePort::Completion, 0));

        // Should only call cleanup once.
        VERIFY_ARE_EQUAL((uint32_t)1, cxt.cleanupCount);

        XTaskQueueCloseHandle(async.queue);
    }

    DEFINE_TEST_CASE(VerifyCompleteInBegin)
    {
        struct Context
        {
            HANDLE evt = nullptr;

            Context()
            {
                evt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
                VERIFY_IS_NOT_NULL(evt);
            }

            ~Context()
            {
                if (evt) CloseHandle(evt);
            }
        };

        Context context;
        XAsyncBlock async{};
        async.queue = queue;
        async.context = &context;
        async.callback = [](XAsyncBlock* async)
        {
            Context* cxt = static_cast<Context*>(async->context);
            SetEvent(cxt->evt);
        };

        auto provider = [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch(op)
            {
                case XAsyncOp::Begin:
                    XAsyncComplete(data->async, S_OK, 0);
                    break;

                case XAsyncOp::Cleanup:
                    break;

                default:
                    VERIFY_FAIL();
                    break;
            }
            return S_OK;
        };

        VERIFY_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));
        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(context.evt, 2500));
    }

    DEFINE_TEST_CASE(VerifyBeginAfterTerminate)
    {
        XAsyncBlock async{};
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &async.queue));

        auto provider = [](XAsyncOp, const XAsyncProviderData*)
        {
            return S_OK;
        };

        // Terminate the queue
        XTaskQueueTerminate(async.queue, true, nullptr, nullptr);

        // XAsyncBegin should fail early with an abort.
        VERIFY_ARE_EQUAL(E_ABORT, XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));

        XTaskQueueCloseHandle(async.queue);
    }

    DEFINE_TEST_CASE(VerifyFailureInDoWork)
    {
        XAsyncBlock async{};
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &async.queue));

        constexpr static HRESULT hrSpecial = 0x8009ABCD;

        auto provider = [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch(op)
            {
                case XAsyncOp::Begin:
                    return XAsyncSchedule(data->async, 0);

                case XAsyncOp::Cleanup:
                    break;

                case XAsyncOp::DoWork:
                    return hrSpecial;

                default:
                    VERIFY_FAIL();
                    break;
            }
            return S_OK;
        };

        // Ensure that the call runs through and correctly reports our special 
        // error.
        VERIFY_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));
        VERIFY_ARE_EQUAL(hrSpecial, XAsyncGetStatus(&async, true));

        XTaskQueueCloseHandle(async.queue);
    }

    DEFINE_TEST_CASE(VerifyDuplicateResultCallsFail)
    {
        XAsyncBlock async = {};
        auto data = AutoRef<FactorialCallData>(new FactorialCallData {});
        DWORD result;
        std::vector<XAsyncOp> ops;

        async.queue = queue;

        data.Ref->value = 5;

        VERIFY_SUCCEEDED(FactorialAsync(data.Ref, &async));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));

        VERIFY_SUCCEEDED(FactorialResult(&async, &result));
        VERIFY_ARE_EQUAL(E_ILLEGAL_METHOD_CALL, FactorialResult(&async, &result));
    }

    DEFINE_TEST_CASE(VerifyDuplicatePendingResultCallsSucceed)
    {
        struct Context
        {
            HANDLE complete = nullptr;
            HANDLE waiting = nullptr;

            Context()
            {
                complete = CreateEvent(nullptr, TRUE, FALSE, nullptr);
                VERIFY_IS_NOT_NULL(complete);

                waiting = CreateEvent(nullptr, TRUE, FALSE, nullptr);
                VERIFY_IS_NOT_NULL(waiting);
            }

            ~Context()
            {
                if (complete) CloseHandle(complete);
                if (waiting) CloseHandle(waiting);
            }
        };        

        auto provider = [](XAsyncOp op, const XAsyncProviderData* data)
        {
            Context* cxt;

            switch(op)
            {
                case XAsyncOp::Begin:
                    return XAsyncSchedule(data->async, 0);

                case XAsyncOp::Cleanup:
                    break;

                case XAsyncOp::DoWork:
                    cxt = (Context*)(data->async->context);
                    SetEvent(cxt->waiting);
                    WaitForSingleObject(cxt->complete, INFINITE);
                    XAsyncComplete(data->async, S_OK, 0);
                    break;

                default:
                    VERIFY_FAIL();
                    break;
            }
            return S_OK;
        };

        Context context;
        XAsyncBlock async{};
        async.queue = queue;
        async.context = &context;

        VERIFY_SUCCEEDED(XAsyncBegin(&async, nullptr, nullptr, nullptr, provider));

        // Wait for the provider to get its do work called
        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(context.waiting, 2000));

        // Now try to access the results -- we should get pending and be able to 
        // do this again and again.

        for (uint32_t idx = 0; idx < 10; idx++)
        {
            VERIFY_ARE_EQUAL(E_PENDING, XAsyncGetResult(&async, nullptr, 0, nullptr, nullptr));
        }

        // Now complete the work
        SetEvent(context.complete);
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, true));

        VERIFY_SUCCEEDED(XAsyncGetResult(&async, nullptr, 0, nullptr, nullptr));

        // Because there was no payload this should continue to succeed
        VERIFY_SUCCEEDED(XAsyncGetResult(&async, nullptr, 0, nullptr, nullptr));
    }
};
