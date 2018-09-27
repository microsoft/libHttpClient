// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// This is the "private" api for an async queue.  It is an immutable definition used to help define
// the implementation of a queue so that multiple implementations can co-habitate and share the same
// flat public API.
#pragma once

enum class ApiId
{
    Identity,
    AsyncQueueSection,
    AsyncQueue
};

// Lightweight version of IUnknown
struct IApi
{
    virtual uint32_t __stdcall AddRef() = 0;
    virtual uint32_t __stdcall Release() = 0;
    virtual HRESULT __stdcall QueryApi(_In_ ApiId id, _Out_ void** ptr) = 0;
};

struct IAsyncQueue;
struct IAsyncQueueSection;

// A queue section is either the work or completion component of
// an async queue
struct IAsyncQueueSection : IApi
{
    virtual HRESULT __stdcall QueueItem(
        _In_ IAsyncQueue* owner,
        _In_ uint32_t waitMs, 
        _In_ void* callbackContext, 
        _In_ AsyncQueueCallback* callback) = 0;

    virtual void __stdcall RemoveItems(
        _In_ AsyncQueueCallback* searchCallback,
        _In_opt_ void* predicateContext,
        _In_ AsyncQueueRemovePredicate* removePredicate) = 0;

    virtual bool __stdcall DrainOneItem() = 0;
    virtual bool __stdcall Wait(_In_ uint32_t timeout) = 0;
    virtual bool __stdcall IsEmpty() = 0;
};

// The async queue.  The public flat API is built entirely on
// this primitive.
struct IAsyncQueue : IApi
{
    virtual HRESULT __stdcall GetSection(
        _In_ AsyncQueueCallbackType type,
        _Out_ IAsyncQueueSection** section) = 0;

    virtual HRESULT __stdcall RegisterSubmitCallback(
        _In_opt_ void* context,
        _In_ AsyncQueueCallbackSubmitted* callback,
        _Out_ registration_token_t* token) = 0;

    virtual void __stdcall UnregisterSubmitCallback(
        _In_ registration_token_t token) = 0;
};

// Defines the structure that backs an async_queue_handle_t.  This structure can never
// change, nor can its signature.
struct async_queue_t
{
    uint32_t m_signature;
    IAsyncQueue* m_queue;
};

static uint32_t const ASYNC_QUEUE_SIGNATURE = 0x41515545;
