// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "../httpcall.h"
#include "xmlhttp_http_task.h"
#include "http_request_callback.h"
#include "http_response_stream.h"

http_response_stream::http_response_stream(
    const std::weak_ptr<xmlhttp_http_task>& httpTask
    ) : m_httpTask(httpTask)
{
}

HRESULT STDMETHODCALLTYPE http_response_stream::Write(
    _In_reads_bytes_(cb) const void *pv,
    _In_ ULONG cb,
    _Out_opt_ ULONG *pcbWritten
    )
{
    auto httpTask = m_httpTask.lock();
    if (httpTask == nullptr)
    {
        // OnError has already been called so just error out
        return STG_E_CANTSAVE;
    }

    if (pcbWritten != nullptr)
    {
        *pcbWritten = 0;
    }

    if (cb == 0)
    {
        return S_OK;
    }

    try
    {
        httpTask->response_buffer().append(pv, cb);
        if (pcbWritten != nullptr)
        {
            *pcbWritten = (ULONG)cb;
        }
        return S_OK;
    }
    catch (...)
    {
        httpTask->set_exception(std::current_exception());
        return STG_E_CANTSAVE;
    }
}

HRESULT STDMETHODCALLTYPE http_response_stream::Read(
    _Out_writes_bytes_to_(cb, *pcbRead) void *pv, 
    _In_ ULONG cb, 
    _Out_ ULONG *pcbRead
    )
{
    UNREFERENCED_PARAMETER(pv);
    UNREFERENCED_PARAMETER(cb);
    UNREFERENCED_PARAMETER(pcbRead);
    return E_NOTIMPL;
}


