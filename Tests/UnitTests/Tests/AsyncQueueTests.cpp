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
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, &complete, CallbackThunk<void, void>::Callback));
        });

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, &work, CallbackThunk<void, void>::Callback));

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

            VERIFY_SUCCEEDED(SubmitAsyncCallback(child, AsyncQueueCallbackType_Work, &calls, [](void* context)
            {
                
                DWORD* pcalls = (DWORD*)context;
                (*pcalls)++;
            }));

            VERIFY_SUCCEEDED(SubmitAsyncCallback(child, AsyncQueueCallbackType_Completion, &calls, [](void* context)
            {
                
                DWORD* pcalls = (DWORD*)context;
                (*pcalls)++;
            }));
        });

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, &work, CallbackThunk<void, void>::Callback));

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

    DEFINE_TEST_CASE(VerifyReferenceQueue)
    {
        async_queue_handle_t queue;

        VERIFY_SUCCEEDED(CreateAsyncQueue(AsyncQueueDispatchMode_Manual, AsyncQueueDispatchMode_Manual, &queue));
        
        for(int idx = 0; idx < 10; idx++)
        {
            queue = DuplicateAsyncQueueHandle(queue);
        }

        for(int idx = 0; idx < 11; idx++)
        {
            CloseAsyncQueue(queue);
        }
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
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, &workThunk, CallbackThunk<void, void>::Callback));
        }

        VERIFY_IS_FALSE(IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work));
        VERIFY_IS_TRUE(IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion));

        for(DWORD idx = 0; idx < count; idx++)
        {
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, &completeThunk, CallbackThunk<void, void>::Callback));
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
    
        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, &workThunk, CallbackThunk<void, void>::Callback));
        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, &workThunk, CallbackThunk<void, void>::Callback));

        VERIFY_IS_TRUE(DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 0));
        VERIFY_ARE_EQUAL((DWORD)1, workCalls);

        SleepEx(0, TRUE);
        VERIFY_ARE_EQUAL((DWORD)2, workCalls);
        VERIFY_ARE_EQUAL(GetCurrentThreadId(), workThreadId);

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, &completeThunk, CallbackThunk<void, void>::Callback));

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
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, &workThunk, CallbackThunk<void, void>::Callback));
        });

        VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, &completeHandoffThunk, CallbackThunk<void, void>::Callback));

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
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, array1 + idx, cb));
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, array2 + idx, cb));
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
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Work, nullptr, cb));
        }

        for(DWORD i = 0; i < completeCount; i++)
        {
            VERIFY_SUCCEEDED(SubmitAsyncCallback(queue, AsyncQueueCallbackType_Completion, nullptr, cb));
        }

        VERIFY_ARE_EQUAL(submitCount.Work, workCount);
        VERIFY_ARE_EQUAL(submitCount.Completion, completeCount);

        UnregisterAsyncQueueCallbackSubmitted(queue, token);
    }
};
