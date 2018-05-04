#pragma once

#include "asyncQueue.h"

#include <mutex>

template<class CallbackType, class CallbackDataType>
struct DefaultThunk
{
    void operator()(CallbackType callback, void* context, CallbackDataType* data)
    {
        callback(context, static_cast<const CallbackDataType*>(data));
    }
};

//
// Callback class -- this class can be used to implement a GameCore style
// notification callback.
//
// Type parameters:
//
//      CallbackType - The type of function pointer callback.
//
//      CallbackDataType -- The data type of the payload of the callback.  The data type
//          may be dynanically allocated and Callback can free it with delete, or
//          the data type must implement operator=.
//
//     CallbackThunk -- A struct with a function operator that can invoke the callback.  
//         The default callback thunk assumes the callback prototype is:
//
//              void CallbackType(void* context, const CallbackDataType* data);
//
template<class CallbackType, class CallbackDataType, class CallbackThunk = DefaultThunk<CallbackType, CallbackDataType>>
class Callback
{
public:
    Callback()
    {
        InitializeListHead(&m_callbackHead);
    }

    ~Callback()
    {
        Clear();
    }

    // Disable copy ctor and assignment, as these cannot be implemented without 
    // potentially throwing exceptions
    Callback<CallbackType, CallbackDataType, CallbackThunk>(const Callback<CallbackType, CallbackDataType, CallbackThunk>&) = delete;

    Callback<CallbackType, CallbackDataType, CallbackThunk>& operator= (const Callback<CallbackType, CallbackDataType, CallbackThunk>&) = delete;

    //
    // Adds a callback function to this callback.
    //
    HRESULT Add(
        _In_opt_ async_queue_handle_t queue,
        _In_opt_ void* context,
        _In_ CallbackType callback,
        _Out_ uint32_t* cookie)
    {
        if (queue == nullptr)
        {
#ifdef _WIN32
            HRESULT hr = CreateSharedAsyncQueue(
                GetCurrentThreadId(),
                AsyncQueueDispatchMode_ThreadPool,
                AsyncQueueDispatchMode_FixedThread,
                &queue);

            if (FAILED(hr))
            {
                return hr;
            }
#else
            RETURN_HR(E_INVALIDARG);
#endif
        }
        else
        {
            ReferenceAsyncQueue(queue);
        }
        
        CallbackRegistration* entry = new (std::nothrow) CallbackRegistration;
        
        if (entry == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        entry->Queue = queue;
        entry->Cookie = m_nextCookie.fetch_add(1);
        entry->Context = context;
        entry->Callback = callback;
        entry->Refs = 1;

        *cookie = entry->Cookie;

        {
            std::lock_guard<std::mutex> lock{ m_lock };
            InsertTailList(&m_callbackHead, &entry->ListEntry);
        }

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
        std::lock_guard<std::mutex> lock{ m_lock };

        PLIST_ENTRY entry = RemoveHeadList(&m_callbackHead);
        while (entry != &m_callbackHead)
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
            entry = RemoveHeadList(&m_callbackHead);
        }
    }

    //
    // If free is true, pointer to data will be freed using delete when callback
    // has run.  If false, CallbackType must be copyable with a copy ctor.  Returns
    // S_OK if successful.
    //
    HRESULT Queue(
        _In_ CallbackDataType* data,
        _In_ bool free)
    {
        HRESULT result = S_OK;
        CallbackSharedData* sharedData = nullptr;

        {
            std::lock_guard<std::mutex> lock{ m_lock };

            PLIST_ENTRY entry = m_callbackHead.Flink;

            while (entry != &m_callbackHead)
            {
                if (sharedData == nullptr && free)
                {
                    sharedData = new (std::nothrow) CallbackSharedData;
                    if (sharedData == nullptr)
                    {
                        result = E_OUTOFMEMORY;
                        break;
                    }

                    sharedData->Data = data;
                    sharedData->Refs = 1;
                }

                CallbackInvocation* invocation = new (std::nothrow) CallbackInvocation;
                if (invocation == nullptr)
                {
                    result = E_OUTOFMEMORY;
                    break;
                }

                CallbackRegistration* cb = CONTAINING_RECORD(entry, CallbackRegistration, ListEntry);
                invocation->Owner = this;
                invocation->Cookie = cb->Cookie;

                if (free)
                {
                    invocation->SharedData = sharedData;
                    sharedData->Refs++;
                }
                else
                {
                    invocation->Data = *data;
                    invocation->SharedData = nullptr;
                }

                result = SubmitAsyncCallback(cb->Queue, AsyncQueueCallbackType_Completion, invocation, OnQueueCallback);
                if (FAILED(result))
                {
                    Destroy(invocation);
                    break;
                }

                entry = entry->Flink;
            }
        }

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
    HRESULT Invoke(
        _In_ CallbackDataType* data)
    {
        uint32_t* cookies = nullptr;
        size_t cookieCount = 0;

        {
            std::lock_guard<std::mutex> lock{ m_lock };

            // Walk through all the callback entries and grab their
            // cookies.

            PLIST_ENTRY entry = m_callbackHead.Flink;
            while (entry != &m_callbackHead)
            {
                cookieCount++;
                entry = entry->Flink;
            }

            if (cookieCount != 0)
            {
                cookies = new (std::nothrow) uint32_t[cookieCount];

                if (cookies != nullptr)
                {
                    entry = m_callbackHead.Flink;
                    cookieCount = 0;
                    while (entry != &m_callbackHead)
                    {
                        CallbackRegistration* cb = CONTAINING_RECORD(entry, CallbackRegistration, ListEntry);
                        cookies[cookieCount] = cb->Cookie;
                        cookieCount++;
                        entry = entry->Flink;
                    }
                }
            }
        }

        if (cookieCount != 0 && cookies == nullptr)
        {
            return E_OUTOFMEMORY;
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

        return S_OK;
    }

private:

    struct CallbackSharedData
    {
        CallbackDataType* Data;
        std::atomic<uint32_t> Refs;
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
        std::atomic<uint32_t> Refs;
        async_queue_handle_t Queue;
        void* Context;
        CallbackType* Callback;
    };

    std::mutex m_lock;
    std::atomic<uint32_t> m_nextCookie{ 1 };
    LIST_ENTRY m_callbackHead;

    void Release(_In_ CallbackSharedData* sharedData)
    {
        if (sharedData->Refs.fetch_sub(1) == 1)
        {
            delete sharedData->Data;
            delete sharedData;
        }
    }

    void Release(_In_ CallbackRegistration* entry)
    {
        if (entry->Refs.fetch_sub(1) == 1)
        {
            Destroy(entry);
        }
    }

    void Destroy(_In_ CallbackRegistration* entry)
    {
        if (entry->Queue != nullptr)
        {
            CloseAsyncQueue(entry->Queue);
        }
        delete entry;
    }

    void Destroy(_In_ CallbackInvocation* invocation)
    {
        if (invocation->SharedData != nullptr)
        {
            Release(invocation->SharedData);
        }

        delete invocation;
    }

    CallbackRegistration* Find(_In_ uint32_t cookie, _In_ bool remove)
    {
        std::lock_guard<std::mutex> lock{ m_lock };

        CallbackRegistration* found = nullptr;
        PLIST_ENTRY entry = m_callbackHead.Flink;
        while (entry != &m_callbackHead)
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
                    found->Refs++;
                }
                break;
            }

            entry = entry->Flink;
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
