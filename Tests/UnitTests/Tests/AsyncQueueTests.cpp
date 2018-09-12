// Copyright(c) Microsoft Corporation. All rights reserved.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "asyncqueue.h"
#include "callbackthunk.h"

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

private:

    H _handle;
    C _closer;

};

struct QueueHandleCloser
{
    void operator ()(async_queue_handle_t h)
    {
        CloseAsyncQueue(h);
    }
};

class AutoQueueHandle : public AutoHandleWrapper<async_queue_handle_t, QueueHandleCloser> {};

struct HandleCloser
{
    void operator()(HANDLE h)
    {
        CloseHandle(h);
    }
};

class AutoHandle : public AutoHandleWrapper<HANDLE, HandleCloser> 
{
public:
    AutoHandle(HANDLE h)
        : AutoHandleWrapper<HANDLE, HandleCloser>(h)
    {}
};

DEFINE_TEST_CLASS(AsyncQueueTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(AsyncQueueTests)

    DEFINE_TEST_CASE(VerifyStockQueue)
    {
        AutoQueueHandle queue;
        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_ThreadPool, AsyncQueueDispatchMode_FixedThread, &queue));

        bool workCalled = false;
        bool completeCalled = false;
        
        AutoHandle wait(CreateEvent(nullptr, TRUE, FALSE, nullptr));
        VERIFY_IS_NOT_NULL(wait);

        CallbackThunk<void, void> complete([&]()
        {
            completeCalled = true;
            SetEvent(wait);
        });

        CallbackThunk<void, void> work([&]()
        {
            workCalled = true;
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, 0, &complete, CallbackThunk<void, void>::Callback));
        });

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, 0, &work, CallbackThunk<void, void>::Callback));

        DWORD waitResult = WaitForSingleObjectEx(wait, 5000, TRUE);
        VERIFY_ARE_EQUAL((DWORD)WAIT_IO_COMPLETION, waitResult);

        waitResult = WaitForSingleObjectEx(wait, 1000, TRUE);
        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, waitResult);
    }

    DEFINE_TEST_CASE(VerifySharedAsyncQueue)
    {
        AutoQueueHandle queue1, queue1b, queue2, queue2b, queue2c;

        VERIFY_SUCCEEDED(CreateSharedAsyncQueue(1, AsyncQueueDispatchMode_ThreadPool, AsyncQueueDispatchMode_FixedThread, &queue1));
        VERIFY_SUCCEEDED(CreateSharedAsyncQueue(1, AsyncQueueDispatchMode_ThreadPool, AsyncQueueDispatchMode_FixedThread, &queue1b));
        VERIFY_ARE_EQUAL((uint64)queue1.Handle(), (uint64)queue1b.Handle());

        VERIFY_SUCCEEDED(CreateSharedAsyncQueue(2, AsyncQueueDispatchMode_ThreadPool, AsyncQueueDispatchMode_FixedThread, &queue2));
        VERIFY_SUCCEEDED(CreateSharedAsyncQueue(2, AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_FixedThread, &queue2b));
        VERIFY_SUCCEEDED(CreateSharedAsyncQueue(2, AsyncQueueDispatchMode_ThreadPool, AsyncQueueDispatchMode_Manual, &queue2c));
        VERIFY_ARE_NOT_EQUAL((uint64)queue2.Handle(), (uint64)queue2b.Handle());
        VERIFY_ARE_NOT_EQUAL((uint64)queue2.Handle(), (uint64)queue2c.Handle());
        VERIFY_ARE_NOT_EQUAL((uint64)queue2b.Handle(), (uint64)queue2c.Handle());

        VERIFY_ARE_NOT_EQUAL((uint64)queue1.Handle(), (uint64)queue2.Handle());
    }

    DEFINE_TEST_CASE(VerifyNestedQueue)
    {
        AutoQueueHandle queue;
        DWORD calls = 0;

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_ThreadPool, AsyncQueueDispatchMode_Manual, &queue));

        CallbackThunk<void, void> work([&]
        {
            calls++;

            // Now create a nested queue and invoke more work and a completion
            // They should all run on the work side.  Queues do not fully shut down until
            // they are empty so we should be fine auto closing the child queue here.

            AutoQueueHandle child;
            VERIFY_SUCCEEDED(CreateNestedAsyncQueue(queue, &child));

            VERIFY_SUCCEEDED(SubmitAsyncCallback(child, AsyncQueueCallbackType_Work, 0, &calls, [](void* context)
            {
                
                DWORD* pcalls = (DWORD*)context;
                (*pcalls)++;
            }));

            VERIFY_SUCCEEDED(SubmitAsyncCallback(child, AsyncQueueCallbackType_Completion, 0, &calls, [](void* context)
            {
                
                DWORD* pcalls = (DWORD*)context;
                (*pcalls)++;
            }));
        });

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, &work, CallbackThunk<void, void>::Callback));

        // Now wait for the queue to drain. The completion side of the queue should never have an item 
        // in it.
        VERIFY_IS_TRUE(IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion));
        UINT64 ticks = GetTickCount64();
        while(!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work)) 
        {
            VERIFY_IS_TRUE(GetTickCount64() - ticks < (UINT64)1000);
            Sleep(100);
        }      
    }

    DEFINE_TEST_CASE(VerifyDuplicateQueueHandle)
    {
        const size_t count = 10;
        async_queue_handle_t queue;
        async_queue_handle_t dups[count];

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));

        for (int idx = 0; idx < count; idx++)
        {
            dups[idx] = DuplicateAsyncQueueHandle(queue);
        }

        for (int idx = 0; idx < count; idx++)
        {
            CloseAsyncQueue(dups[idx]);
        }
        CloseAsyncQueue(queue);
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

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));

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
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, &workThunk, CallbackThunk<void, void>::Callback));
        }

        VERIFY_IS_FALSE(IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work));
        VERIFY_IS_TRUE(IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion));

        for(DWORD idx = 0; idx < count; idx++)
        {
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, 0, &completeThunk, CallbackThunk<void, void>::Callback));
        }

        VERIFY_IS_FALSE(IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion));

        while(DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 0))
        {
            dispatched++;
        }

        VERIFY_ARE_EQUAL(count, dispatched);
        VERIFY_ARE_EQUAL(count, workCalls);

        dispatched = 0;

        while(DispatchAsyncQueue(queue, AsyncQueueCallbackType_Completion, 0))
        {
            dispatched++;
        }

        VERIFY_ARE_EQUAL(count, dispatched);
        VERIFY_ARE_EQUAL(count, workCalls);
        VERIFY_ARE_EQUAL(count, completeCalls);

        queue.Close();
        workCalls = 0;
        completeCalls = 0;
        dispatched = 0;

        // Note: inverting who has the fixed thread and who has the thread pool for variety

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_FixedThread, AsyncQueueDispatchMode_ThreadPool, &queue));
    
        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, &workThunk, CallbackThunk<void, void>::Callback));
        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, &workThunk, CallbackThunk<void, void>::Callback));

        VERIFY_IS_TRUE(DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 0));
        VERIFY_ARE_EQUAL((DWORD)1, workCalls);

        SleepEx(0, TRUE);
        VERIFY_ARE_EQUAL((DWORD)2, workCalls);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), workThreadId);

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, 0, &completeThunk, CallbackThunk<void, void>::Callback));

        UINT64 ticks = GetTickCount64();
        while(!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion)) 
        {
            VERIFY_IS_TRUE(GetTickCount64() - ticks < (UINT64)1000);
            Sleep(100);
        }

        VERIFY_ARE_EQUAL((DWORD)1, completeCalls);
        VERIFY_ARE_NOT_EQUAL(GetCurrentThreadId(), completeThreadId);

        workCalls = completeCalls = workThreadId = completeThreadId = 0;

        CallbackThunk<void, void> completeHandoffThunk([&]()
        {
            completeCalls++;
            completeThreadId = GetCurrentThreadId();
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, &workThunk, CallbackThunk<void, void>::Callback));
        });

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, 0, &completeHandoffThunk, CallbackThunk<void, void>::Callback));

        ticks = GetTickCount64();
        while(!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion)) 
        {
            VERIFY_IS_TRUE(GetTickCount64() - ticks < (UINT64)1000);
            SleepEx(100, TRUE);
        }

        ticks = GetTickCount64();
        while(!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work)) 
        {
            VERIFY_IS_TRUE(GetTickCount64() - ticks < (UINT64)1000);
            SleepEx(100, TRUE);
        }

        VERIFY_ARE_EQUAL(GetCurrentThreadId(), workThreadId);
    }

    DEFINE_TEST_CASE(VerifyRemoveCallbacks)
    {
        AutoQueueHandle queue;
        const DWORD count = 5;

        DWORD array1[count];
        DWORD array2[count];

        ZeroMemory(array1, sizeof(DWORD) * count);
        ZeroMemory(array2, sizeof(DWORD) * count);

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));

        auto cb = [](void* cxt)
        {
            DWORD* ptr = (DWORD*)cxt;
            *ptr = 1;
        };

        for(DWORD idx = 0; idx < count; idx++)
        {
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, array1 + idx, cb));
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, array2 + idx, cb));
        }

        CallbackThunk<void*, bool> search([&](void* cxt)
        {
            return cxt >= array2 && cxt < array2 + count;
        });

        RemoveAsyncQueueCallbacks(queue, AsyncQueueCallbackType_Work, cb, &search, CallbackThunk<void*, bool>::Callback);

        while(DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 100));

        for(DWORD idx = 0; idx < count; idx++)
        {
            VERIFY_ARE_EQUAL(array1[idx], (DWORD)1);
            VERIFY_ARE_EQUAL(array2[idx], (DWORD)0);
        }
    }

    DEFINE_TEST_CASE(VerifySubmittedCallback)
    {
        AutoQueueHandle queue;
        registration_token_t token;
        const DWORD workCount = 4;
        const DWORD completeCount = 7;

        struct SubmitCount
        {
            DWORD Work;
            DWORD Completion;
        } submitCount;

        submitCount.Work = submitCount.Completion = 0;

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));

        VERIFY_SUCCEEDED(RegisterAsyncQueueCallbackSubmitted(queue, &submitCount, [](void* cxt, async_queue_handle_t, AsyncQueueCallbackType type)
        {
            SubmitCount* s = (SubmitCount*)cxt;
            if (type == AsyncQueueCallbackType_Work)
            {
                s->Work++;
            }
            else
            {
                s->Completion++;
            }
        }, &token));

        auto cb = [](void*) {};
        
        for(DWORD i = 0; i < workCount; i++)
        {
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, cb));
        }

        for(DWORD i = 0; i < completeCount; i++)
        {
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, 0, nullptr, cb));
        }

        VERIFY_ARE_EQUAL(submitCount.Work, workCount);
        VERIFY_ARE_EQUAL(submitCount.Completion, completeCount);

        UnregisterAsyncQueueCallbackSubmitted(queue, token);
    }

    DEFINE_TEST_CASE(VerifySubmitCallbackWithWait)
    {
        AutoQueueHandle queue;

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));

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

        AsyncQueueCallbackType types[] =
        {
            AsyncQueueCallbackType_Work,
            AsyncQueueCallbackType_Completion
        };

        auto cb = [](void* context)
        {
            ArgData* data = (ArgData*)context;
            data->Data->Times[data->Index] = GetTickCount64();
        };

        for (int i = 0; i < _countof(types); i++)
        {
            AsyncQueueCallbackType type = types[i];
            uint64_t baseTicks = GetTickCount64();

            ArgData call1;
            call1.Index = 0;
            call1.Data = &result;
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, type, 1000, &call1, cb));

            ArgData call2;
            call2.Index = 1;
            call2.Data = &result;
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, type, 0, &call2, cb));

            ArgData call3;
            call3.Index = 2;
            call3.Data = &result;
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, type, 500, &call3, cb));

            // We should be able to dispatch one without waiting
            VERIFY_IS_TRUE(DispatchAsyncQueue(queue, type, 0));
            VERIFY_IS_FALSE(DispatchAsyncQueue(queue, type, 0));

            VERIFY_IS_TRUE(DispatchAsyncQueue(queue, type, 700));
            VERIFY_IS_TRUE(DispatchAsyncQueue(queue, type, 1200));
            VERIFY_IS_FALSE(DispatchAsyncQueue(queue, type, 0));

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
        registration_token_t tokens[count];
        uint32_t calls[count];

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));

        auto cb = [](void* context, async_queue_handle_t, AsyncQueueCallbackType)
        {
            uint32_t* p = static_cast<uint32_t*>(context);
            (*p)++;
        };

        auto dummy = [](void*) {};

        for (uint32_t idx = 0; idx < count; idx++)
        {
            calls[idx] = 0;
            VERIFY_SUCCEEDED(RegisterAsyncQueueCallbackSubmitted(queue, &(calls[idx]), cb, &tokens[idx]));
        }

        // queue some calls
        SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, dummy);
        SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, dummy);
        SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, dummy);

        // Should be a correct count on all calls
        for (uint32_t idx = 0; idx < count; idx++)
        {
            VERIFY_ARE_EQUAL(calls[idx], 3u);
        }

        // Nuke every odd entry
        for (uint32_t idx = 1; idx < count; idx += 2)
        {
            UnregisterAsyncQueueCallbackSubmitted(queue, tokens[idx]);
        }

        // Now make some more calls.
        SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, dummy);
        SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, dummy);
        SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, 0, nullptr, dummy);

        // Should be a correct count on all calls
        for (uint32_t idx = 0; idx < count; idx++)
        {
            uint32_t expectedCount = (idx & 1) ? 3 : 6;
            VERIFY_ARE_EQUAL(calls[idx], expectedCount);
        }

        // Dispatch all calls on the queue so we can shut it down
        while (DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 0));
    }
};
