// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#include "XTaskQueue.h"
#include "CallbackThunk.h"
#include "PumpedTaskQueue.h"
#include "XTaskQueuePriv.h"

#define TEST_CLASS_OWNER L"brianpe"

namespace ApiDiag
{
    extern std::atomic<uint32_t> g_globalApiRefs;
}

template <class H, class C>
class AutoHandleWrapper
{
public:

    AutoHandleWrapper()
        : _handle(nullptr)
    {
    }

    AutoHandleWrapper(H h)
        : _handle(h)
    {
    }

    ~AutoHandleWrapper()
    {
        Close();
    }

    H Handle() const
    {
        return _handle;
    }

    void Close()
    {
        if (_handle != nullptr)
        {
            _closer(_handle);
            _handle = nullptr;
        }
    }

    AutoHandleWrapper& operator=(H h)
    {
        Close();
        _handle = h;
        return *this;
    }

    H* operator&()
    {
        return &_handle;
    }

    operator H() const
    {
        return _handle;
    }

    operator bool() const
    {
        return _handle != nullptr;
    }

    H Release()
    {
        H h = _handle;
        _handle = nullptr;
        return h;
    }

private:

    H _handle;
    C _closer;

};

struct QueueHandleCloser
{
    void operator ()(XTaskQueueHandle h)
    {
        XTaskQueueCloseHandle(h);
    }
};

typedef  AutoHandleWrapper<XTaskQueueHandle, QueueHandleCloser> AutoQueueHandle;

struct HandleCloser
{
    void operator()(HANDLE h)
    {
        CloseHandle(h);
    }
};

typedef AutoHandleWrapper<HANDLE, HandleCloser> AutoHandle;

DEFINE_TEST_CLASS(TaskQueueTests)
{
public:

#ifdef USING_TAEF

    BEGIN_TEST_CLASS(TaskQueueTests)
    END_TEST_CLASS()

    TEST_CLASS_CLEANUP(ClassCleanup)
    {
        //
        // Note: this is a global refcount for tracking
        // leaks in the task queue.  If any other tests fail
        // this may also fail, as those tests could have leaked.
        //
        uint32_t gr = ApiDiag::g_globalApiRefs;
        VERIFY_ARE_EQUAL(0u, gr);
        return true;
    }

#else
    DEFINE_TEST_CLASS_PROPS(TaskQueueTests);

    TEST_CLASS_CLEANUP(ClassCleanup)
    {
        uint32_t gr = ApiDiag::g_globalApiRefs;
        VERIFY_ARE_EQUAL(0u, gr);
    }

#endif

    DEFINE_TEST_CASE(VerifyStockQueue)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::Manual, &queue));

        bool workCalled = false;
        bool completeCalled = false;
        
        CallbackThunk<void, void> complete([&]()
        {
            completeCalled = true;
        });

        CallbackThunk<void, void> work([&]()
        {
            workCalled = true;
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &complete, CallbackThunk<void, void>::Callback));
        });

        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &work, CallbackThunk<void, void>::Callback));

        VERIFY_IS_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 5000));
        VERIFY_IS_TRUE(workCalled);
        VERIFY_IS_TRUE(completeCalled);
    }

    DEFINE_TEST_CASE(VerifyCompositeQueue)
    {
        AutoQueueHandle queue;
        DWORD calls = 0;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::Manual, &queue));

        CallbackThunk<void, void> work([&]
        {
            calls++;

            // Now create a composite queue with work and completion pointing to the work
            // stream of the original queue and invoke more work and a completion
            // They should all run on the work side.  Queues do not fully shut down until
            // they are empty so we should be fine auto closing the child queue here.

            XTaskQueuePortHandle workStream;
            VERIFY_SUCCEEDED(XTaskQueueGetPort(queue.Handle(), XTaskQueuePort::Work, &workStream));

            AutoQueueHandle composite;
            VERIFY_SUCCEEDED(XTaskQueueCreateComposite(workStream, workStream, &composite));

            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(composite, XTaskQueuePort::Work, 0, &calls, [](void* context, bool)
            {
                DWORD* pcalls = (DWORD*)context;
                (*pcalls)++;
            }));
        });

        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &work, CallbackThunk<void, void>::Callback));

        // Now wait for the queue to drain. The completion side of the queue should never have an item 
        // in it.
        VERIFY_IS_TRUE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion));
        UINT64 ticks = GetTickCount64();
        while(!XTaskQueueIsEmpty(queue, XTaskQueuePort::Work)) 
        {
            VERIFY_IS_LESS_THAN(GetTickCount64() - ticks, (UINT64)1000);
            Sleep(100);
        }      
    }

    DEFINE_TEST_CASE(VerifyDuplicateQueueHandle)
    {
        const size_t count = 10;
        XTaskQueueHandle queue;
        XTaskQueueHandle dups[count];

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));
        
        for(int idx = 0; idx < count; idx++)
        {
            VERIFY_SUCCEEDED(XTaskQueueDuplicateHandle(queue, &dups[idx]));
        }

        for(int idx = 0; idx < count; idx++)
        {
            XTaskQueueCloseHandle(dups[idx]);
        }
        XTaskQueueCloseHandle(queue);
    }

    DEFINE_TEST_CASE(VerifyDispatch)
    {
        AutoQueueHandle queue;
        DWORD workCalls = 0;
        DWORD completeCalls = 0;
        DWORD dispatched = 0;
        DWORD workThreadId = 0;
        DWORD completeThreadId = 0;
        const DWORD count = 10;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        CallbackThunk<void, void> workThunk([&]()
        {
            workCalls++;
            workThreadId = GetCurrentThreadId();
        });

        CallbackThunk<void, void> completeThunk([&]()
        {
            completeCalls++;
            completeThreadId = GetCurrentThreadId();
        });

        for(DWORD idx = 0; idx < count; idx++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk, CallbackThunk<void, void>::Callback));
        }

        VERIFY_IS_FALSE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Work));
        VERIFY_IS_TRUE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion));

        for(DWORD idx = 0; idx < count; idx++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &completeThunk, CallbackThunk<void, void>::Callback));
        }

        VERIFY_IS_FALSE(XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion));

        while(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0))
        {
            dispatched++;
        }

        VERIFY_ARE_EQUAL(count, dispatched);
        VERIFY_ARE_EQUAL(count, workCalls);

        dispatched = 0;

        while(XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
        {
            dispatched++;
        }

        VERIFY_ARE_EQUAL(count, dispatched);
        VERIFY_ARE_EQUAL(count, workCalls);
        VERIFY_ARE_EQUAL(count, completeCalls);

        LOG_COMMENT(L"Verify thread pool and fixed thread dispatch");
        
        queue.Close();
        workCalls = 0;
        completeCalls = 0;
        dispatched = 0;

        // Note: inverting who has the manual thread and who has the thread pool for variety

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::ThreadPool, &queue));
    
        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk, CallbackThunk<void, void>::Callback));
        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk, CallbackThunk<void, void>::Callback));

        VERIFY_IS_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
        VERIFY_ARE_EQUAL((DWORD)1, workCalls);

        VERIFY_IS_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
        VERIFY_ARE_EQUAL((DWORD)2, workCalls);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), workThreadId);

        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &completeThunk, CallbackThunk<void, void>::Callback));

        UINT64 ticks = GetTickCount64();
        while(!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion)) 
        {
            VERIFY_IS_LESS_THAN(GetTickCount64() - ticks, (UINT64)1000);
            Sleep(100);
        }

        VERIFY_ARE_EQUAL((DWORD)1, completeCalls);
        VERIFY_ARE_NOT_EQUAL(GetCurrentThreadId(), completeThreadId);

        LOG_COMMENT(L"Verify correct fixed thread");

        workCalls = completeCalls = workThreadId = completeThreadId = 0;

        CallbackThunk<void, void> completeHandoffThunk([&]()
        {
            completeCalls++;
            completeThreadId = GetCurrentThreadId();
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, &workThunk, CallbackThunk<void, void>::Callback));
        });

        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &completeHandoffThunk, CallbackThunk<void, void>::Callback));

        ticks = GetTickCount64();
        while(!XTaskQueueIsEmpty(queue, XTaskQueuePort::Completion)) 
        {
            VERIFY_IS_LESS_THAN(GetTickCount64() - ticks, (UINT64)1000);
            XTaskQueueDispatch(queue, XTaskQueuePort::Work, 100);
        }

        ticks = GetTickCount64();
        while(!XTaskQueueIsEmpty(queue, XTaskQueuePort::Work)) 
        {
            VERIFY_IS_LESS_THAN(GetTickCount64() - ticks, (UINT64)1000);
            XTaskQueueDispatch(queue, XTaskQueuePort::Work, 100);
        }

        VERIFY_ARE_EQUAL(GetCurrentThreadId(), workThreadId);
    }

    DEFINE_TEST_CASE(VerifySubmittedCallback)
    {
        AutoQueueHandle queue;
        XTaskQueueRegistrationToken token;
        const DWORD workCount = 4;
        const DWORD completeCount = 7;

        struct SubmitCount
        {
            DWORD Work;
            DWORD Completion;
        } submitCount;

        submitCount.Work = submitCount.Completion = 0;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(queue, &submitCount, [](void* cxt, XTaskQueueHandle, XTaskQueuePort stream)
        {
            SubmitCount* s = (SubmitCount*)cxt;
            if (stream == XTaskQueuePort::Work)
            {
                s->Work++;
            }
            else
            {
                s->Completion++;
            }
        }, &token));

        auto cb = [](void*, bool) {};
        
        for(DWORD i = 0; i < workCount; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));
        }

        for(DWORD i = 0; i < completeCount; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Completion, nullptr, cb));
        }

        VERIFY_ARE_EQUAL(submitCount.Work, workCount);
        VERIFY_ARE_EQUAL(submitCount.Completion, completeCount);

        XTaskQueueUnregisterMonitor(queue, token);
    
        // Now drain the queues
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
    }

    DEFINE_TEST_CASE(VerifySubmitCallbackWithWait)
    {
        AutoQueueHandle queue;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        struct ResultData
        {
            uint64_t Times[3];
        };

        struct ArgData
        {
            ResultData* Data;
            int Index;
        };

        ResultData result;

        XTaskQueuePort streams[] =
        {
            XTaskQueuePort::Work,
            XTaskQueuePort::Completion
        };

        auto cb = [](void* context, bool)
        {
            ArgData* data = (ArgData*)context;
            data->Data->Times[data->Index] = GetTickCount64();
        };

        for (int i = 0; i < _countof(streams); i++)
        {
            XTaskQueuePort stream = streams[i];
            uint64_t baseTicks = GetTickCount64();

            ArgData call1;
            call1.Index = 0;
            call1.Data = &result;
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, stream, 1000, &call1, cb));

            ArgData call2;
            call2.Index = 1;
            call2.Data = &result;
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, stream, 0, &call2, cb));

            ArgData call3;
            call3.Index = 2;
            call3.Data = &result;
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, stream, 500, &call3, cb));

            // We should be able to dispatch one without waiting
            VERIFY_IS_TRUE(XTaskQueueDispatch(queue, stream, 0));
            VERIFY_IS_FALSE(XTaskQueueDispatch(queue, stream, 0));

            VERIFY_IS_TRUE(XTaskQueueDispatch(queue, stream, 700));
            VERIFY_IS_TRUE(XTaskQueueDispatch(queue, stream, 1200));
            VERIFY_IS_FALSE(XTaskQueueDispatch(queue, stream, 0));

            uint64_t call1Ticks = result.Times[0] - baseTicks;
            uint64_t call2Ticks = result.Times[1] - baseTicks;
            uint64_t call3Ticks = result.Times[2] - baseTicks;

            // Call 1 at index 0 should have a tick count > 1000 and < 1050 (shouldn't take 50ms)
            VERIFY_IS_TRUE(call1Ticks >= 1000 && call1Ticks < 1050);
            VERIFY_IS_TRUE(call2Ticks < 50);
            VERIFY_IS_TRUE(call3Ticks >= 500 && call3Ticks < 550);
        }
    }

    DEFINE_TEST_CASE(VerifyRegisterCallbackSubmitted)
    {
        AutoQueueHandle queue;
        const uint32_t count = 5;
        XTaskQueueRegistrationToken tokens[count];
        uint32_t calls[count];

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        auto cb = [](void* context, XTaskQueueHandle, XTaskQueuePort)
        {
            uint32_t* p = static_cast<uint32_t*>(context);
            (*p)++;
        };

        auto dummy = [](void*, bool) { };

        LOG_COMMENT(L"Registering %d callbacks", count);
        for (uint32_t idx = 0; idx < count; idx++)
        {
            calls[idx] = 0;
            VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(queue, &(calls[idx]), cb, &tokens[idx]));
        }

        // queue some calls
        LOG_COMMENT(L"Queuing calls");
        XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
        XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
        XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);

        // Should be a correct count on all calls
        LOG_COMMENT(L"Verifying call count");
        for (uint32_t idx = 0; idx < count; idx++)
        {
            LOG_COMMENT(L"   %d -> %d", idx, calls[idx]);
            VERIFY_ARE_EQUAL(calls[idx], 3u);
        }

        // Nuke every odd entry
        for (uint32_t idx = 1; idx < count; idx += 2)
        {
            LOG_COMMENT(L"Unregistering callback %d", idx);
            XTaskQueueUnregisterMonitor(queue, tokens[idx]);
        }

        // Now make some more calls.
        LOG_COMMENT(L"Queuing calls");
        XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
        XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);
        XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 0, nullptr, dummy);

        // Should be a correct count on all calls
        LOG_COMMENT(L"Verifying call count");
        for (uint32_t idx = 0; idx < count; idx++)
        {
            uint32_t expectedCount = (idx & 1) ? 3 : 6;
            LOG_COMMENT(L"   %d -> %d (expected %d)", idx, calls[idx], expectedCount);
            VERIFY_ARE_EQUAL(calls[idx], expectedCount);
        }

        // Dispatch all calls on the queue so we can shut it down
        while(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
    }

    DEFINE_TEST_CASE(VerifyImmediateDispatch)
    {
        AutoQueueHandle queue;
        uint32_t callCount = 0;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Immediate, &queue));

        auto callback = [](void* ptr, bool)
        {
            uint32_t* pint = (uint32_t*)ptr;
            (*pint)++;
        };

        const uint32_t count = 10;

        for (uint32_t i = 1; i <= count; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &callCount, callback));
            VERIFY_ARE_EQUAL(i, callCount);
        }

        // Verify a deferred completion still works
        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 200, &callCount, callback));
        VERIFY_ARE_EQUAL(count, callCount);
        Sleep(500);
        VERIFY_ARE_EQUAL(count + 1, callCount);
    }

    DEFINE_TEST_CASE(VerifySerializedThreadPoolDispatch)
    {
        AutoQueueHandle queue;
        const uint32_t total = 100;
        struct Data
        {
            uint32_t Count = 0;
            uint32_t Work[total];
        };

        struct PerCallData
        {
            uint32_t Index;
            Data* D;
        };

        Data data;
        data.Count = 0;
        ZeroMemory(data.Work, sizeof(data.Work));

        PerCallData callData[total];
        for (uint32_t i = 0; i < total; i++)
        {
            callData[i].Index = i;
            callData[i].D = &data;
        }

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::SerializedThreadPool, &queue));

        auto callback = [](void* ptr, bool)
        {
            PerCallData* pdata = (PerCallData*)ptr;
            if (pdata->Index == 0)
            {
                pdata->D->Work[pdata->Index] = 5;
            }
            else
            {
                pdata->D->Work[pdata->Index] = pdata->D->Work[pdata->D->Count - 1] + 5;
            }
            pdata->D->Count++;
        };

        for (uint32_t i = 0; i < total; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Completion, 0, &(callData[i]), callback));
        }

        Sleep(500);

        VERIFY_ARE_EQUAL(total, data.Count);
        uint32_t previous = 0;
        for (uint32_t i = 0; i < total; i++)
        {
            VERIFY_ARE_EQUAL(previous + 5, data.Work[i]);
            previous = data.Work[i];
        }
    }

    DEFINE_TEST_CASE(VerifyMultithreadManualDispatch)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Immediate, &queue));

        // We use a manual dispatch queue and spin up two threads to process it.  We are testing
        // that XTaskQueueDispatch never returns with an INFINITE timeout unless the task queue is
        // getting terminated.

        auto threadCallback = [](PVOID param) -> DWORD
        {
            XTaskQueueHandle queue = static_cast<XTaskQueueHandle>(param);

            while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, INFINITE))
            {
                LOG_COMMENT(L"Dispatched item on TID %d", GetCurrentThreadId());
            }

            LOG_COMMENT(L"Thread %d quitting", GetCurrentThreadId());
            return 0;
        };

        DWORD tid;
        AutoHandle thread1 = CreateThread(nullptr, 0, threadCallback, queue.Handle(), 0, &tid);
        AutoHandle thread2 = CreateThread(nullptr, 0, threadCallback, queue.Handle(), 0, &tid);

        VERIFY_IS_NOT_NULL(thread1.Handle());
        VERIFY_IS_NOT_NULL(thread2.Handle());

        HANDLE threadWaits[] = { thread1.Handle(), thread2.Handle() };

        auto workCallback = [](void*, bool)
        {
            LOG_COMMENT(L"Worker callback on TID %d", GetCurrentThreadId());
        };

        LOG_COMMENT(L"Scenario : Verify calls do not abort threads");

        const uint32_t count = 5;
        for (uint32_t i = 1; i <= count; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, workCallback));
        }

        VERIFY_IS_TRUE(WaitForMultipleObjects(2, threadWaits, FALSE, 750) == WAIT_TIMEOUT);

#ifdef SUSPEND_API

        LOG_COMMENT(L"Scenario : Verify global suspend / resume does not abort threads");

        XTaskQueueGlobalSuspend();

        for (uint32_t i = 1; i <= count; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, workCallback));
        }

        XTaskQueueGlobalResume();

        VERIFY_IS_TRUE(WaitForMultipleObjects(2, threadWaits, FALSE, 750) == WAIT_TIMEOUT);
#endif

        LOG_COMMENT(L"Scenario : Verify composite queue termination does not abort threads");

        XTaskQueuePortHandle workPort;
        VERIFY_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Work, &workPort));

        XTaskQueuePortHandle completionPort;
        VERIFY_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Completion, &completionPort));

        AutoQueueHandle compositeQueue;
        VERIFY_SUCCEEDED(XTaskQueueCreateComposite(workPort, completionPort, &compositeQueue));

        for (uint32_t i = 1; i <= count; i++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(compositeQueue, XTaskQueuePort::Work, nullptr, workCallback));
        }

        VERIFY_SUCCEEDED(XTaskQueueTerminate(compositeQueue, true, nullptr, nullptr));

        VERIFY_IS_TRUE(WaitForMultipleObjects(2, threadWaits, FALSE, 750) == WAIT_TIMEOUT);

        LOG_COMMENT(L"Scenario : Verify queue termination aborts both threads");

        VERIFY_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));

        VERIFY_IS_TRUE(WaitForSingleObject(threadWaits[0], 750) == WAIT_OBJECT_0);
        VERIFY_IS_TRUE(WaitForSingleObject(threadWaits[1], 750) == WAIT_OBJECT_0);
    }

    DEFINE_TEST_CASE(VerifyRegisterWithAutoReset)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
        _VerifyRegisterWithAutoReset(queue);

        PumpedTaskQueue pumpedQueue;
        _VerifyRegisterWithAutoReset(pumpedQueue.queue);
    }

    void _VerifyRegisterWithAutoReset(XTaskQueueHandle queue)
    {
        HANDLE workEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        HANDLE completionEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        struct Context
        {
            HANDLE signaled;
            uint32_t count;
        };

        Context workContext;
        workContext.signaled = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        workContext.count = 0;
        
        Context completionContext;
        completionContext.signaled = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        completionContext.count = 0;

        auto cb = [](void* cxt, bool)
        {
            Context* c = (Context*)cxt;
            c->count++;
            SetEvent(c->signaled);
        };

        XTaskQueueRegistrationToken workToken = {};
        XTaskQueueRegistrationToken completionToken = {};

        VERIFY_SUCCEEDED(XTaskQueueRegisterWaiter(queue, XTaskQueuePort::Work, workEvent, &workContext, cb, &workToken));
        VERIFY_SUCCEEDED(XTaskQueueRegisterWaiter(queue, XTaskQueuePort::Completion, completionEvent, &completionContext, cb, &completionToken));

        for (uint32_t idx = 1; idx <= 5; idx++)
        {
            SetEvent(workEvent);
            VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(workContext.signaled, 1000));
            VERIFY_ARE_EQUAL((DWORD)WAIT_TIMEOUT, WaitForSingleObject(workContext.signaled, 100));
            VERIFY_ARE_EQUAL(idx, workContext.count);
        }

        for (uint32_t idx = 1; idx <= 5; idx++)
        {
            SetEvent(completionEvent);
            VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(completionContext.signaled, 1000));
            VERIFY_ARE_EQUAL((DWORD)WAIT_TIMEOUT, WaitForSingleObject(completionContext.signaled, 100));
            VERIFY_ARE_EQUAL(idx, completionContext.count);
        }

        VERIFY_ARE_EQUAL(5u, workContext.count);
        VERIFY_ARE_EQUAL(5u, completionContext.count);

        XTaskQueueUnregisterWaiter(queue, workToken);
        XTaskQueueUnregisterWaiter(queue, completionToken);

        CloseHandle(workContext.signaled);
        CloseHandle(completionContext.signaled);
        CloseHandle(workEvent);
        CloseHandle(completionEvent);
    }

    DEFINE_TEST_CASE(VerifyQueueTermination)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
        LOG_COMMENT(L"** ThreadPool queue, no wait **");
        _VerifyQueueTermination(queue, false, false, false);

        queue.Close();
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
        LOG_COMMENT(L"** ThreadPool queue, wait **");
        _VerifyQueueTermination(queue, true, false, false);

        queue.Close();
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::SerializedThreadPool, XTaskQueueDispatchMode::SerializedThreadPool, &queue));
        LOG_COMMENT(L"** ThreadPool queue, wait, serialized **");
        _VerifyQueueTermination(queue, true, true, false);
    }

    DEFINE_TEST_CASE(VerifyEmptyQueueTermination)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
        LOG_COMMENT(L"** ThreadPool queue, no wait **");
        _VerifyQueueTermination(queue, false, false, true);

        queue.Close();
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));
        LOG_COMMENT(L"** ThreadPool queue, wait **");
        _VerifyQueueTermination(queue, true, false, true);

        queue.Close();
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::SerializedThreadPool, XTaskQueueDispatchMode::SerializedThreadPool, &queue));
        LOG_COMMENT(L"** ThreadPool queue, wait, serialized **");
        _VerifyQueueTermination(queue, true, true, true);
    }

    void _VerifyQueueTermination(XTaskQueueHandle queue, bool wait, bool serialized, bool empty)
    {
        struct Data
        {
            std::atomic<uint32_t> workCount = { };
            std::atomic<uint32_t> completionCount = { };
            bool serialized;
            XTaskQueueHandle queue;
            XTaskQueueCallback* completionCallback;
        };

        auto workCb = [](void* cxt, bool cancel)
        {
            Data* data = (Data*)cxt;

            if (data->serialized && !cancel)
            {
                // The very first work item may come in when we first start 
                // the pump threads.  Sleep so we have a chance to enter
                // the termination code.
                VERIFY_ARE_EQUAL(0u, data->workCount);
                Sleep(1000);
            }

            data->workCount++;
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(data->queue, XTaskQueuePort::Completion, data, data->completionCallback));
        };

        auto completionCb = [](void* cxt, bool cancel)
        {
            Data* data = (Data*)cxt;
            VERIFY_IS_TRUE(!data->serialized || cancel);
            data->completionCount++;
        };

        std::vector<HANDLE> events;

        Data data;
        data.serialized = serialized;
        data.queue = queue;
        data.completionCallback = completionCb;

        uint32_t normalCount = 0;
        uint32_t futureCount = 0;
        uint32_t eventCount = 0;

        if (!empty)
        {
            normalCount = 5;
            futureCount = 5;
            eventCount = 5;

            for(uint32_t idx = 0; idx < normalCount; idx++)
            {
                VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, &data, workCb));
            }

            for(uint32_t idx = 0; idx < futureCount; idx++)
            {
                VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, 10000, &data, workCb));
            }

            for(uint32_t idx = 0; idx < eventCount; idx++)
            {
                HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                VERIFY_IS_NOT_NULL(evt);
                events.push_back(evt);
                XTaskQueueRegistrationToken token;

                VERIFY_SUCCEEDED(XTaskQueueRegisterWaiter(queue, XTaskQueuePort::Work, evt, &data, workCb, &token));
            }
        }

        if (wait)
        {
            VERIFY_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));
        }
        else
        {
            HANDLE evt = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            VERIFY_IS_NOT_NULL(evt);

            auto termCb = [](void* cxt)
            {
                HANDLE h = (HANDLE)cxt;
                SetEvent(h);
            };

            VERIFY_SUCCEEDED(XTaskQueueTerminate(queue, false, evt, termCb));
            VERIFY_ARE_NOT_EQUAL((DWORD)WAIT_TIMEOUT, WaitForSingleObject(evt, 5000));
            CloseHandle(evt);
        }

        VERIFY_ARE_EQUAL(
            E_ABORT, 
            XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, &data, workCb));

        VERIFY_ARE_EQUAL(
            E_ABORT, 
            XTaskQueueSubmitCallback(queue, XTaskQueuePort::Completion, &data, completionCb));

        for(auto h : events)
        {
            CloseHandle(h);
        }

        uint32_t expectedCount = normalCount + futureCount + eventCount;
        VERIFY_ARE_EQUAL(expectedCount, data.workCount.load());
        VERIFY_ARE_EQUAL(expectedCount, data.completionCount.load());
    }

    DEFINE_TEST_CASE(VerifyGlobalQueue)
    {
        AutoQueueHandle queue;
        VERIFY_IS_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&queue));
        XTaskQueueHandle globalQueue = queue;
        VERIFY_IS_NOT_NULL(queue);

        auto cb = [](void*, bool) {};

        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));

        // The global queue should not be closable or terminatable.
        XTaskQueueCloseHandle(queue);
        XTaskQueueCloseHandle(queue);
        XTaskQueueCloseHandle(queue);
        XTaskQueueCloseHandle(queue);
        XTaskQueueCloseHandle(queue);

        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));

        VERIFY_ARE_EQUAL(E_ACCESSDENIED, XTaskQueueTerminate(queue, false, nullptr, nullptr));

        // Now replace the global with our own.
        AutoQueueHandle ourQueue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &ourQueue));

        XTaskQueueSetCurrentProcessTaskQueue(ourQueue);

        queue.Close();
        VERIFY_IS_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&queue));
        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, nullptr, cb));
        VERIFY_IS_FALSE(XTaskQueueIsEmpty(ourQueue, XTaskQueuePort::Work));
        while(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) {};

        // Null the queue and verify we get false
        queue.Close();
        XTaskQueueSetCurrentProcessTaskQueue(nullptr);
        VERIFY_IS_FALSE(XTaskQueueGetCurrentProcessTaskQueue(&queue));
        VERIFY_IS_NULL(queue.Handle());

        // Replace the global queue back
        XTaskQueueSetCurrentProcessTaskQueue(globalQueue);
    }

    DEFINE_TEST_CASE(VerifyGlobalQueueTermination)
    {
        AutoQueueHandle globalQueue;
        VERIFY_IS_TRUE(XTaskQueueGetCurrentProcessTaskQueue(&globalQueue));
        VERIFY_IS_NOT_NULL(globalQueue);
        
        VERIFY_ARE_EQUAL(E_ACCESSDENIED, XTaskQueueTerminate(globalQueue, true, nullptr, nullptr));
        
        // We should be able to create a composite off of this queue and
        // terminate the composite safely.
        
        XTaskQueuePortHandle port;
        VERIFY_SUCCEEDED(XTaskQueueGetPort(globalQueue, XTaskQueuePort::Work, &port));
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreateComposite(port, port, &queue));
        VERIFY_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));
        
        // And termination of the composite should have no affect on the global queue.
        
        bool called = false;
        auto cb = [](void* context, bool) 
        {
            bool* called = static_cast<bool*>(context);
            *called = true;
        };

        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(globalQueue, XTaskQueuePort::Work, &called, cb));
        while (XTaskQueueDispatch(globalQueue, XTaskQueuePort::Work, 0)) {}

        VERIFY_IS_TRUE(called);
    }

    DEFINE_TEST_CASE(VerifyCloseInTerminationForThreadPool)
    {
        struct TestData
        {
            AutoQueueHandle queue;
            bool terminationCalled = false;

        } data;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &data.queue));

        VERIFY_SUCCEEDED(XTaskQueueTerminate(data.queue, false, &data, [](void* context)
        {
            TestData* pd = (TestData*)context;
            XTaskQueueCloseHandle(pd->queue.Release());
            pd->terminationCalled = true;
        }));

        uint64_t ticks = GetTickCount64();
        while (!data.terminationCalled && GetTickCount64() - ticks < 5000)
        {
            Sleep(250);
        }

        VERIFY_IS_TRUE(data.terminationCalled);
    }

    DEFINE_TEST_CASE(VerifyCloseInTerminationForManual)
    {
        struct TestData
        {
            AutoQueueHandle queue;
            bool terminationCalled = false;

        } data;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &data.queue));

        VERIFY_SUCCEEDED(XTaskQueueTerminate(data.queue, false, &data, [](void* context)
        {
            TestData* pd = (TestData*)context;
            pd->terminationCalled = true;
            XTaskQueueCloseHandle(pd->queue.Release());
        }));

        uint64_t ticks = GetTickCount64();
        while (!data.terminationCalled && GetTickCount64() - ticks < 5000)
        {
            XTaskQueueDispatch(data.queue, XTaskQueuePort::Work, 0);
            XTaskQueueDispatch(data.queue, XTaskQueuePort::Completion, 0);
            Sleep(250);
        }

        VERIFY_IS_TRUE(data.terminationCalled);
    }

    DEFINE_TEST_CASE(VerifyWaitTermination)
    {
        uint64_t start = GetTickCount64();
        do
        {
            XTaskQueueHandle queue;
            HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
            if (FAILED(hr)) VERIFY_FAIL();

            hr = XTaskQueueTerminate(queue, true, nullptr, nullptr);
            if (FAILED(hr)) VERIFY_FAIL();

            XTaskQueueCloseHandle(queue);
        } while(GetTickCount64() - start < 5000);
    }

    DEFINE_TEST_CASE(VerifyManualDispatchAtTermination)
    {
        AutoQueueHandle queue;

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        auto dispatcher = [](void*, XTaskQueueHandle queue, XTaskQueuePort port)
        {
            XTaskQueueDispatch(queue, port, 0);
        };

        XTaskQueueRegistrationToken token;
        VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(queue, nullptr, dispatcher, &token));
        VERIFY_SUCCEEDED(XTaskQueueTerminate(queue, true, nullptr, nullptr));
    }

    DEFINE_TEST_CASE(VerifyTerminationOfCompositeQueue)
    {
        struct TestData
        {
            AutoQueueHandle queue;
            bool workInvoked = false;
            bool workCanceled = false;
            bool completionInvoked = false;
            bool completionCanceled = false;
            bool futureInvoked = false;
            bool futureCanceled = false;
            bool waitInvoked = false;
            bool waitCanceled = false;
            bool queueTerminated = false;
            XTaskQueueCallback* CompletionCallback = nullptr;
        } data, compositeData;

        AutoHandle waitHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        VERIFY_IS_NOT_NULL(waitHandle);

        AutoHandle compositeWaitHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        VERIFY_IS_NOT_NULL(compositeWaitHandle);

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &data.queue));

        XTaskQueuePortHandle port;
        VERIFY_SUCCEEDED(XTaskQueueGetPort(data.queue, XTaskQueuePort::Work, &port));
        VERIFY_SUCCEEDED(XTaskQueueCreateComposite(port, port, &compositeData.queue));

        auto workCallback = [](void* context, bool canceled)
        {
            TestData* pd = (TestData*)context;
            pd->workInvoked = true;
            pd->workCanceled = canceled;
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(pd->queue, XTaskQueuePort::Completion, pd, pd->CompletionCallback));
        };

        auto futureCallback = [](void* context, bool canceled)
        {
            TestData* pd = (TestData*)context;
            pd->futureInvoked = true;
            pd->futureCanceled = canceled;
        };

        auto waitCallback = [](void* context, bool canceled)
        {
            TestData* pd = (TestData*)context;
            pd->waitInvoked = true;
            pd->waitCanceled = canceled;
        };

        auto completionCallback = [](void* context, bool canceled)
        {
            TestData* pd = (TestData*)context;
            pd->completionInvoked = true;
            pd->completionCanceled = canceled;
        };

        auto terminationCallback = [](void* context)
        {
            TestData* pd = (TestData*)context;
            pd->queueTerminated = true;
            XTaskQueueCloseHandle(pd->queue.Release());
        };

        data.CompletionCallback = completionCallback;
        compositeData.CompletionCallback = completionCallback;

        // Submit work callbacks to both queues and terminate the composite.
        XTaskQueueRegistrationToken token;

        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(data.queue, XTaskQueuePort::Work, &data, workCallback));
        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(data.queue, XTaskQueuePort::Work, 300, &data, futureCallback));
        VERIFY_SUCCEEDED(XTaskQueueRegisterWaiter(data.queue, XTaskQueuePort::Work, waitHandle, &data, waitCallback, &token));

        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(compositeData.queue, XTaskQueuePort::Work, &compositeData, workCallback));
        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(compositeData.queue, XTaskQueuePort::Work, 300, &compositeData, futureCallback));
        VERIFY_SUCCEEDED(XTaskQueueRegisterWaiter(compositeData.queue, XTaskQueuePort::Work, compositeWaitHandle, &compositeData, waitCallback, &token));

        VERIFY_SUCCEEDED(XTaskQueueTerminate(compositeData.queue, false, &compositeData, terminationCallback));

        bool somethingDispatched;
        do
        {
            somethingDispatched = XTaskQueueDispatch(data.queue, XTaskQueuePort::Work, 500);
            somethingDispatched |= XTaskQueueDispatch(data.queue, XTaskQueuePort::Completion, 500);
        } while (somethingDispatched);

        // Verify -- calls for the main queue should have gone through but calls for the composite
        // should have been canceled.

        VERIFY_IS_TRUE(data.workInvoked);
        VERIFY_IS_TRUE(data.completionInvoked);
        VERIFY_IS_TRUE(data.futureInvoked);
        VERIFY_IS_FALSE(data.waitInvoked);
        VERIFY_IS_FALSE(data.workCanceled);
        VERIFY_IS_FALSE(data.completionCanceled);
        VERIFY_IS_FALSE(data.futureCanceled);

        VERIFY_IS_TRUE(compositeData.workCanceled);
        VERIFY_IS_TRUE(compositeData.completionCanceled);
        VERIFY_IS_TRUE(compositeData.futureCanceled);
        VERIFY_IS_TRUE(compositeData.waitCanceled);
        VERIFY_IS_TRUE(compositeData.queueTerminated);

        // Verify it is still possible to schedule a call on the main queue and that registrations remain
        data.workInvoked = false;
        data.workCanceled = false;
        data.waitInvoked = false;
        data.waitCanceled = false;
        compositeData.waitInvoked = false;
        compositeData.waitCanceled = false;
        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(data.queue, XTaskQueuePort::Work, &data, workCallback));

        SetEvent(waitHandle);
        SetEvent(compositeWaitHandle);

        do
        {
            somethingDispatched = XTaskQueueDispatch(data.queue, XTaskQueuePort::Work, 500);
            somethingDispatched |= XTaskQueueDispatch(data.queue, XTaskQueuePort::Completion, 500);
        } while (somethingDispatched);

        VERIFY_IS_TRUE(data.workInvoked);
        VERIFY_IS_TRUE(data.waitInvoked);
        VERIFY_IS_FALSE(data.workCanceled);
        VERIFY_IS_FALSE(data.waitCanceled);
        VERIFY_IS_FALSE(compositeData.waitInvoked);
    }

    DEFINE_TEST_CASE(VerifyQueueMonitorHasCorrectPorts)
    {
        AutoQueueHandle queue, compositeQueue;

        XTaskQueuePort queuePort = (XTaskQueuePort)(-1);
        XTaskQueuePort compositeQueuePort = (XTaskQueuePort)(-1);

        auto cb = [](void*, bool) {};

        auto monitorCallback = [](void* context, XTaskQueueHandle, XTaskQueuePort port)
        {
            XTaskQueuePort* pp = (XTaskQueuePort*)context;
            *pp = port;
        };

        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        XTaskQueuePortHandle port;
        VERIFY_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Work, &port));
        VERIFY_SUCCEEDED(XTaskQueueCreateComposite(port, port, &compositeQueue));

        XTaskQueueRegistrationToken token;
        VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(queue, &queuePort, monitorCallback, &token));
        VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(compositeQueue, &compositeQueuePort, monitorCallback, &token));

        // Submitting a call to the composite port should generate a monitor callback on each
        // queue with the correct port ID.
        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(compositeQueue, XTaskQueuePort::Completion, nullptr, cb));
        while (XTaskQueueDispatch(compositeQueue, XTaskQueuePort::Completion, 100));

        VERIFY_ARE_EQUAL(queuePort, XTaskQueuePort::Work);
        VERIFY_ARE_EQUAL(compositeQueuePort, XTaskQueuePort::Completion);
    }

    static void NextStep(void* context, uint32_t expectedStep, PCWSTR stepName)
    {
        uint32_t* step = static_cast<uint32_t*>(context);
        LOG_COMMENT(L"%u: %ws (%u)", expectedStep, stepName, *step);
        VERIFY_ARE_EQUAL(expectedStep, *step);
        (*step)++;
    };
    
    DEFINE_TEST_CASE(VerifyNestedQueueDispatchOrder)
    {
        auto Nest = [](XTaskQueueHandle q) -> XTaskQueueHandle
        {
            XTaskQueuePortHandle port;
            VERIFY_SUCCEEDED(XTaskQueueGetPort(q, XTaskQueuePort::Work, &port));

            XTaskQueueHandle newQueue;
            VERIFY_SUCCEEDED(XTaskQueueCreateComposite(port, port, &newQueue));
            return newQueue;
        };

        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        AutoQueueHandle outer(Nest(queue));
        AutoQueueHandle inner(Nest(outer));

        uint32_t step = 0;

        NextStep(&step, 0, L"Submitting Tasks");

        VERIFY_SUCCEEDED(XTaskQueueSubmitDelayedCallback(outer, XTaskQueuePort::Work, 3000, &step, [](void* context, bool)
        {
            NextStep(context, 6, L"Delayed task on outer queue");
        }));

        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(inner, XTaskQueuePort::Work, &step, [](void* context, bool)
        {
            NextStep(context, 2, L"Task on inner queue");
        }));

        NextStep(&step, 1, L"About to tick queues");

        // Note that when dispatching on a queue we're actually dispathing
        // the queues port, so this may dispatch outer, inner or main.
        XTaskQueueDispatch(outer, XTaskQueuePort::Work, 0);
        XTaskQueueDispatch(outer, XTaskQueuePort::Completion, 0);

        NextStep(&step, 3, L"Closing inner queue");
        XTaskQueueCloseHandle(inner.Release());

        NextStep(&step, 4, L"Tick queues again");
        XTaskQueueDispatch(outer, XTaskQueuePort::Work, 0);
        XTaskQueueDispatch(outer, XTaskQueuePort::Completion, 0);

        NextStep(&step, 5, L"Wait for outer to finish");

        XTaskQueueDispatch(outer, XTaskQueuePort::Work, 4000);
        XTaskQueueDispatch(outer, XTaskQueuePort::Completion, 1000);

        NextStep(&step, 7, L"Closing outer queue");
        XTaskQueueCloseHandle(outer.Release());
    }

    DEFINE_TEST_CASE(VerifyTerminateWaitsForSuspends)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue));

        AutoHandle waitHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        VERIFY_IS_NOT_NULL(waitHandle);

        auto terminatedCallback = [](void* context)
        {
            HANDLE h = (HANDLE)context;
            SetEvent(h);
        };

        VERIFY_SUCCEEDED(XTaskQueueSuspendTermination(queue));
        VERIFY_SUCCEEDED(XTaskQueueTerminate(queue, false, waitHandle.Handle(), terminatedCallback));

        VERIFY_ARE_EQUAL((DWORD)WAIT_TIMEOUT, WaitForSingleObject(waitHandle, 2000));

        XTaskQueueResumeTermination(queue);

        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(waitHandle, 2000));
    }

#ifdef SUSPEND_API
    DEFINE_TEST_CASE(VerifySuspendResume)
    {
        constexpr uint32_t queueCount = 10;

        auto monitor = [](void* context, XTaskQueueHandle, XTaskQueuePort)
        {
            LOG_COMMENT(L"TaskQueue Monitor fired");
            HANDLE waitHandle = static_cast<HANDLE>(context);
            SetEvent(waitHandle);
        };

        auto task = [](void* context, bool)
        {
            LOG_COMMENT(L"TaskQueue Task fired");
            HANDLE waitHandle = static_cast<HANDLE>(context);
            SetEvent(waitHandle);
        };

        struct QueueItem
        {
            AutoQueueHandle queue;
            AutoHandle monitorHandle;
            AutoHandle taskHandle;
        } items[queueCount];

        for (uint32_t idx = 0; idx < queueCount; idx++)
        {
            VERIFY_SUCCEEDED(XTaskQueueCreate(
                XTaskQueueDispatchMode::ThreadPool,
                 XTaskQueueDispatchMode::ThreadPool,
                 &items[idx].queue));

            items[idx].monitorHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            VERIFY_IS_NOT_NULL(items[idx].monitorHandle);

            items[idx].taskHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
            VERIFY_IS_NOT_NULL(items[idx].taskHandle);

            XTaskQueueRegistrationToken token;
            VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(
                items[idx].queue.Handle(),
                items[idx].monitorHandle.Handle(),
                monitor, &token));
        }

        // Global suspend all task queues
        uint64_t ticks = GetTickCount64();
        XTaskQueueGlobalSuspend();
        LOG_COMMENT(L"Ticks to suspend: %I64u\n", GetTickCount64() - ticks);

        for (uint32_t idx = 0; idx < queueCount; idx++)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(
                items[idx].queue.Handle(),
                XTaskQueuePort::Work,
                items[idx].taskHandle.Handle(),
                task));
        }

        // While suspended we should see none of these get called.
        HANDLE handles[queueCount * 2];
        for (uint32_t idx = 0; idx < queueCount; idx++)
        {
            handles[idx] = items[idx].taskHandle;
            handles[idx + queueCount] = items[idx].monitorHandle;
        }

        DWORD result = WaitForMultipleObjects(queueCount * 2, handles, FALSE, 1000);
        VERIFY_ARE_EQUAL((DWORD)WAIT_TIMEOUT, result);

        // Manual dispatching should act like nothing is in the queue
        for (uint32_t idx = 0; idx < queueCount; idx++)
        {
            VERIFY_IS_FALSE(XTaskQueueDispatch(items[idx].queue.Handle(), XTaskQueuePort::Work, 250));
        }

        // Now release the suspend.  Both the monitor and the call should 
        // run.
        XTaskQueueGlobalResume();

        result = WaitForMultipleObjects(queueCount * 2, handles, TRUE, 1000);
        VERIFY_ARE_NOT_EQUAL((DWORD)WAIT_TIMEOUT, result);
    }

    DEFINE_TEST_CASE(VerifySuspendResumeWithManualQueue)
    {
        struct ThreadState
        {
            AutoQueueHandle queue;
            std::condition_variable workActivity;
            std::mutex workMutex;
            bool terminate = false;
            bool monitorSignaled = false;
            AutoHandle thread;

            ~ThreadState()
            {
                Terminate();
            }

            void Terminate()
            {
                if (thread)
                {
                    terminate = true;
                    workActivity.notify_all();
                    WaitForSingleObject(thread, INFINITE);
                    // Handle close comes from AutoHandle
                }
            }
        };

        // Simple worker thread that waits to be
        // signaled and then runs calls.

        auto workThread = [](_In_ PVOID param) -> DWORD
        {
            ThreadState* state = static_cast<ThreadState*>(param);

            std::unique_lock<std::mutex> lock(state->workMutex);

            while (!state->terminate)
            {
                state->workActivity.wait(lock);
                LOG_COMMENT(L".....Worker awake and dispatching one call");
                XTaskQueueDispatch(state->queue, XTaskQueuePort::Work, 0);
                state->monitorSignaled = false;
            }

            return 0;
        };

        // Task queue monitor callback that signals the work thread to wake
        // when a call comes in.

        auto monitor = [](void* context, XTaskQueueHandle, XTaskQueuePort)
        {
            LOG_COMMENT(L".....TaskQueue Monitor fired");
            ThreadState* state = static_cast<ThreadState*>(context);
            std::unique_lock<std::mutex> lock(state->workMutex);
            state->monitorSignaled = true;
            state->workActivity.notify_all();
        };

        // Actual task callback.  This just signals an event so the test
        // knows the task was invoked.

        auto task = [](void* context, bool)
        {
            LOG_COMMENT(L".....TaskQueue Task fired");
            HANDLE handle = static_cast<HANDLE>(context);
            SetEvent(handle);
        };

        ThreadState threadState;
        VERIFY_SUCCEEDED(XTaskQueueCreate(
            XTaskQueueDispatchMode::ThreadPool,
            XTaskQueueDispatchMode::Manual,
            &threadState.queue));

        threadState.thread = CreateThread(
            nullptr,
            0,
            workThread,
            &threadState,
            0,
            nullptr);
        WEX::Common::Throw::IfNull(threadState.thread);

        XTaskQueueRegistrationToken token;
        VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(
            threadState.queue,
            &threadState,
            monitor,
            &token));

        // Returns true if the task event is signaled

        auto checkSignaled = [](HANDLE taskEvent) -> bool
        {
            return WaitForSingleObject(taskEvent, 750) == WAIT_OBJECT_0;
        };

        // OK, now we are ready to do some calls. First try the non-suspended case
        // to ensure the plumbing is setup right.

        AutoHandle taskEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        WEX::Common::Throw::IfNull(taskEvent);

        LOG_COMMENT(L"\nSCENARIO: Baseline call");
        VERIFY_IS_FALSE(checkSignaled(taskEvent));
        VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(
            threadState.queue,
            XTaskQueuePort::Work,
            taskEvent.Handle(),
            task));

        VERIFY_IS_TRUE(checkSignaled(taskEvent));

        // task event is auto reset; verify
        VERIFY_IS_FALSE(checkSignaled(taskEvent));

        // Suspended case:  suspend the queue and then submit a couple callbacks.
        // The callbacks should not be invoked during suspension but
        // should pick back up when resumed.

        LOG_COMMENT(L"\nSCENARIO: Calls scheduled during suspend");

        // Current bug where we may orphan a call if more than a couple are
        // pending, so run several here.
        AutoHandle taskEvents[5];

        for (auto &e : taskEvents)
        {
            e = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            WEX::Common::Throw::IfNull(e);
        }

        XTaskQueueGlobalSuspend();

        for (auto &e : taskEvents)
        {
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(
                threadState.queue,
                XTaskQueuePort::Work,
                e.Handle(),
                task));
        }

        for (auto &e : taskEvents)
        {
            VERIFY_IS_FALSE(checkSignaled(e));
        }

        // The wake event shouldn't have been signaled
        VERIFY_IS_FALSE(threadState.monitorSignaled);

        LOG_COMMENT(L".....Resume queue....call should resume");
        XTaskQueueGlobalResume();

        // Resume may take a small amount of time to asynchronously
        // start the task queues back up.

        Sleep(800);

        for (auto &e : taskEvents)
        {
            VERIFY_IS_TRUE(checkSignaled(e));
        }

        // Next suspended case. Verify that new calls can come
        // in and be scheduled after a suspend / resume cycle.

        LOG_COMMENT(L"\nSCENARIO: Schedule call after suspend");

        for (auto &e : taskEvents)
        {
            VERIFY_IS_FALSE(checkSignaled(e));
            VERIFY_SUCCEEDED(XTaskQueueSubmitCallback(
                threadState.queue,
                XTaskQueuePort::Work,
                e.Handle(),
                task));
        }

        for (auto &e : taskEvents)
        {
            VERIFY_IS_TRUE(checkSignaled(e));
        }
    }
#endif
};
