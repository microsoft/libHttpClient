// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "threadpool.h"
#include "asyncop.h"
#include "mem.h"

using namespace Windows::Foundation;

class Win32Event
{
public:
    Win32Event();
    ~Win32Event();
    void Set();
    void WaitForever();

private:
    HANDLE m_event;
};

template<typename T1, typename T2>
class HttpTaskWithProgress : public std::enable_shared_from_this< HttpTaskWithProgress<T1, T2> >
{
public:
    IAsyncOperationWithProgress<T1, T2>^ m_asyncOp;
    AsyncStatus m_status;
    Win32Event m_event;
    T1 m_result;

    static std::shared_ptr<HttpTaskWithProgress<T1, T2>> New()
    {
        return std::make_shared<HttpTaskWithProgress<T1, T2>>();
        //return std::allocate_shared< HttpTaskWithProgress<T1, T2>, http_stl_allocator<HttpTaskWithProgress<T1, T2> >();
    }

    HttpTaskWithProgress()
    {
    }

    void Init(IAsyncOperationWithProgress<T1, T2>^ asyncOp)
    {
        m_status = AsyncStatus::Started;
        m_asyncOp = asyncOp;
    }

    void WaitForever()
    {
        std::weak_ptr< HttpTaskWithProgress<T1, T2> > thisWeakPtr = shared_from_this();

        m_asyncOp->Completed = ref new AsyncOperationWithProgressCompletedHandler<T1, T2>(
            [thisWeakPtr](IAsyncOperationWithProgress<T1, T2>^ asyncOp, AsyncStatus status)
        {
            std::shared_ptr< HttpTaskWithProgress<T1, T2> > pThis(thisWeakPtr.lock());
            if (pThis)
            {
                pThis->m_status = status;
                pThis->m_result = asyncOp->GetResults();
                pThis->m_event.Set();
            }
        });

        m_event.WaitForever();
    }

    T1 GetResult()
    {
        return m_result;
    }

};
