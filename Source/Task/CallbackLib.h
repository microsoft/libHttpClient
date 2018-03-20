#pragma once

#include "httpClient/AsyncQueue.h"
#include "EntryList.h"

template<class CallbackType, class CallbackDataType>
struct DefaultThunk
{
    void operator()(CallbackType callback, void* context, CallbackDataType* data)
    {
        callback(context, data);
    }
};

//
// Callback class -- this class can be used to implement a GameCore style
// notification callback.
//
// Type parameters:
//
//      CallbackType - The type of function pointer callback.  The callback must have the 
//          following prototype:
//
//              void CallbackType(void* context, CallbackDataType* data);
//
//      CallbackDataType -- The data type of the payload of the callback.  The data type
//          may be dynanically allocated and Callback can free it with delete, or
//          the data type must implement operator=.
//
template<class CallbackType, class CallbackDataType, class CallbackThunk = DefaultThunk<CallbackType, CallbackDataType>>
class Callback
{
public:
    Callback()
        : _nextCookie(1)
    {
        InitializeListHead(&_callbackHead);
        InitializeSRWLock(&_lock);
    }

    ~Callback()
    {
        Clear();
    }

    //
    // Adds a callback function to this callback.
    //
    HRESULT Add(
        _In_opt_ async_queue_t queue, 
        _In_opt_ void* context, 
        _In_ CallbackType callback, 
        _Out_ uint32_t* cookie)
    {
        HRESULT hr;
        CallbackRegistration* entry = new (std::nothrow) CallbackRegistration;

        if (entry == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        if (queue == nullptr)
        {
            hr = CreateSharedAsyncQueue(
                GetCurrentThreadId(),
                AsyncQueueDispatchMode_ThreadPool,
                AsyncQueueDispatchMode_FixedThread,
                &queue);
        }
        else
        {
            hr = ReferenceAsyncQueue(queue);
        }

        if (FAILED(hr))
        {
            Destroy(entry);
            return hr;
        }

        entry->Queue = queue;
        entry->Cookie = InterlockedIncrement(&_nextCookie);
        entry->Context = context;
        entry->Callback = callback;
        entry->Refs = 1;

        *cookie = entry->Cookie;

        AcquireSRWLockExclusive(&_lock);
        InsertTailList(&_callbackHead, &entry->ListEntry);
        ReleaseSRWLockExclusive(&_lock);

        return S_OK;
    }

    //
    // Removes a callback for the given cookie.
    //
    void Remove(_In_ uint32_t cookie)
    {
        CallbackRegistration* remove = Find(cookie, true);

        if (remove != nullptr)
        {
            if (remove->Queue != nullptr)
            {
                RemoveAsyncQueueCallbacks(remove->Queue, AsyncQueueCallbackType_Completion, OnQueueCallback, this, [](void* pCxt, void* cCxt)
                {
                    CallbackInvocation* invocation = (CallbackInvocation*)(cCxt);
                    if (invocation->Owner == pCxt)
                    {
                        invocation->Owner->Destroy(invocation);
                        return true;
                    }
                    return false;
                });
            }

            Release(remove);
        }
    }

    //
    // Clears all callbacks.
    //
    void Clear()
    {
        AcquireSRWLockExclusive(&_lock);

        PLIST_ENTRY entry = RemoveHeadList(&_callbackHead);
        while (entry != &_callbackHead)
        {
            CallbackRegistration* ce = CONTAINING_RECORD(entry, CallbackRegistration, ListEntry);

            if (ce->Queue != nullptr)
            {
                RemoveAsyncQueueCallbacks(ce->Queue, AsyncQueueCallbackType_Completion, OnQueueCallback, this, [](void* pCxt, void* cCxt)
                {
                    CallbackInvocation* invocation = (CallbackInvocation*)(cCxt);
                    if (invocation->Owner == pCxt)
                    {
                        invocation->Owner->Destroy(invocation);
                        return true;
                    }
                    return false;
                });
            }

            Destroy(ce);
            entry = RemoveHeadList(&_callbackHead);
        }

        ReleaseSRWLockExclusive(&_lock);
    }

    //
    // If free is true, pointer to data will be freed using delete when callback
    // has run.  If false, CallbackType must be copyable with a copy ctor.  Returns
    // true if queue was successful, or false if not (IE, out of memory).
    //
    bool Queue(
        _In_ CallbackDataType* data, 
        _In_ bool free)
    {
        AcquireSRWLockShared(&_lock);

        bool result = true;
        CallbackSharedData* sharedData = nullptr;
        PLIST_ENTRY entry = _callbackHead.Flink;

        while (entry != &_callbackHead)
        {
            if (sharedData == nullptr && free)
            {
                sharedData = new (std::nothrow) CallbackSharedData;
                if (sharedData == nullptr)
                {
                    result = false;
                    goto Cleanup;
                }

                sharedData->Data = data;
                sharedData->Refs = 1;
            }

            CallbackInvocation* invocation = new (std::nothrow) CallbackInvocation;
            if (invocation == nullptr)
            {
                result = false;
                goto Cleanup;
            }

            CallbackRegistration* cb = CONTAINING_RECORD(entry, CallbackRegistration, ListEntry);
            invocation->Owner = this;
            invocation->Cookie = cb->Cookie;

            if (free)
            {
                invocation->SharedData = sharedData;
                InterlockedIncrement(&sharedData->Refs);
            }
            else
            {
                invocation->Data = *data;
                invocation->SharedData = nullptr;
            }

            if (FAILED(SubmitAsyncCallback(cb->Queue, AsyncQueueCallbackType_Completion, invocation, OnQueueCallback)))
            {
                result = false;
                Destroy(invocation);
                goto Cleanup;
            }

            entry = entry->Flink;
        }

    Cleanup:

        ReleaseSRWLockShared(&_lock);

        if (sharedData != nullptr)
        {
            Release(sharedData);
        }
        else if (free)
        {
            delete data;
        }

        return result;
    }

    //
    // Invokes the callbacks directly without queuing.
    //
    bool Invoke(
        _In_ CallbackDataType* data)
    {
        uint32_t* cookies = nullptr;
        size_t cookieCount = 0;

        AcquireSRWLockShared(&_lock);

        // Walk through all the callback entries and grab their
        // cookies.

        PLIST_ENTRY entry = _callbackHead.Flink;
        while (entry != &_callbackHead)
        {
            cookieCount++;
            entry = entry->Flink;
        }

        if (cookieCount != 0)
        {
            cookies = new (std::nothrow) uint32_t[cookieCount];

            if (cookies != nullptr)
            {
                entry = _callbackHead.Flink;
                cookieCount = 0;
                while (entry != &_callbackHead)
                {
                    CallbackRegistration* cb = CONTAINING_RECORD(entry, CallbackRegistration, ListEntry);
                    cookies[cookieCount] = cb->Cookie;
                    cookieCount++;
                    entry = entry->Flink;
                }
            }
        }

        ReleaseSRWLockShared(&_lock);

        if (cookieCount != 0 && cookies == nullptr)
        {
            return false;
        }

        for (uint32_t idx = 0; idx < cookieCount; idx++)
        {
            CallbackRegistration* cb = Find(cookies[idx], false);
            if (cb != nullptr)
            {
                CallbackThunk thunk;
                thunk(cb->Callback, cb->Context, data);
                Release(cb);
            }
        }

        delete[] cookies;

        return true;
    }

private:

    struct CallbackSharedData
    {
        CallbackDataType* Data;
        LONG Refs;
    };

    struct CallbackInvocation
    {
        Callback<CallbackType, CallbackDataType, CallbackThunk>* Owner;
        uint32_t Cookie;
        CallbackDataType Data;
        CallbackSharedData *SharedData;
    };

    struct CallbackRegistration
    {
        LIST_ENTRY ListEntry;
        uint32_t Cookie;
        LONG Refs;
        async_queue_t Queue;
        void* Context;
        CallbackType* Callback;
    };

    SRWLOCK _lock;
    uint32_t _nextCookie;
    LIST_ENTRY _callbackHead;

    void Release(CallbackSharedData* sharedData)
    {
        if (InterlockedDecrement(&sharedData->Refs) == 0)
        {
            delete sharedData->Data;
            delete sharedData;
        }
    }

    void Release(CallbackRegistration* entry)
    {
        if (InterlockedDecrement(&entry->Refs) == 0)
        {
            Destroy(entry);
        }
    }

    void Destroy(CallbackRegistration* entry)
    {
        if (entry->Queue != nullptr)
        {
            CloseAsyncQueue(entry->Queue);
        }
        delete entry;
    }

    void Destroy(CallbackInvocation* invocation)
    {
        if (invocation->SharedData != nullptr)
        {
            Release(invocation->SharedData);
        }

        delete invocation;
    }

    CallbackRegistration* Find(uint32_t cookie, bool remove)
    {
        if (remove)
        {
            AcquireSRWLockExclusive(&_lock);
        }
        else
        {
            AcquireSRWLockShared(&_lock);
        }

        CallbackRegistration* found = nullptr;
        PLIST_ENTRY entry = _callbackHead.Flink;
        while (entry != &_callbackHead)
        {
            CallbackRegistration* ce = CONTAINING_RECORD(entry, CallbackRegistration, ListEntry);
            if (ce->Cookie == cookie)
            {
                found = ce;

                if (remove)
                {
                    RemoveEntryList(entry);
                }
                else
                {
                    InterlockedIncrement(&found->Refs);
                }
                break;
            }

            entry = entry->Flink;
        }

        if (remove)
        {
            ReleaseSRWLockExclusive(&_lock);
        }
        else
        {
            ReleaseSRWLockShared(&_lock);
        }

        return found;
    }

    static void CALLBACK OnQueueCallback(_In_ void* context)
    {
        CallbackInvocation* invocation = reinterpret_cast<CallbackInvocation*>(context);
        CallbackRegistration* entry = invocation->Owner->Find(invocation->Cookie, false);
        if (entry != nullptr)
        {
            CallbackDataType* payload;

            if (invocation->SharedData != nullptr)
            {
                payload = invocation->SharedData->Data;
            }
            else
            {
                payload = &invocation->Data;
            }

            CallbackThunk thunk;
            thunk(entry->Callback, entry->Context, payload);
            invocation->Owner->Release(entry);
        }

        invocation->Owner->Destroy(invocation);
    }
};

