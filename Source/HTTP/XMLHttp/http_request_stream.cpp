// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "../httpcall.h"
#include "xmlhttp_http_task.h"
#include "http_request_callback.h"
#include "http_request_stream.h"

http_request_stream::http_request_stream() :
    m_remainingToRead(0),
    m_startIndex(0)
{
}

HRESULT http_request_stream::init(
    _In_reads_bytes_(cb) const BYTE* requestBody,
    _In_ uint32_t requestBodyBytes
    )
{
    try
    {
        m_requestBody.assign(requestBody, requestBody + requestBodyBytes);
        m_remainingToRead = requestBodyBytes;
        m_startIndex = 0;
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

HRESULT STDMETHODCALLTYPE http_request_stream::Write(
    _In_reads_bytes_(cb) const void *pv,
    _In_ ULONG cb,
    _Out_opt_ ULONG *pcbWritten
    )
{
    UNREFERENCED_PARAMETER(pv);
    UNREFERENCED_PARAMETER(cb);
    UNREFERENCED_PARAMETER(pcbWritten);
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE http_request_stream::Read(
    _Out_writes_bytes_to_(cb, *pcbRead) void *pv,
    _In_ ULONG cb,
    _Out_ ULONG *pcbRead
    )
{
    try
    {
        size_t size_to_read = static_cast<size_t>(cb);
        size_to_read = (size_to_read < 0) ? 0 : size_to_read;
        size_to_read = (size_to_read > m_remainingToRead) ? m_remainingToRead : size_to_read;
        errno_t err = memcpy_s(pv, cb, &m_requestBody.front() + m_startIndex, size_to_read);
        if (err && size_to_read != 0)
        {
            return STG_E_READFAULT;
        }
        *pcbRead = static_cast<ULONG>(size_to_read);
        m_remainingToRead -= size_to_read;
        m_startIndex += size_to_read;

        return S_OK;
    }
    catch (...)
    {
        return STG_E_READFAULT;
    }
}


