// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "../httpcall.h"
#include "xmlhttp_http_task.h"
#include "http_request_stream.h"

http_request_stream::http_request_stream() :
    m_call(nullptr),
    m_startIndex(0)
{
}

http_request_stream::~http_request_stream()
{
    if (m_call != nullptr)
    {
        HCHttpCallCloseHandle(m_call);
    }
}

HRESULT http_request_stream::init(
    _In_ HCCallHandle call
    )
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    m_call = HCHttpCallDuplicateHandle(call);
    m_startIndex = 0;
    return S_OK;
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
    if (pv == nullptr || pcbRead == nullptr)
    {
        return STG_E_INVALIDPOINTER;
    }

    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t bodySize = 0;
    void* context = nullptr;
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(m_call, &readFunction, &bodySize, &context);
    if (FAILED(hr) || readFunction == nullptr)
    {
        return STG_E_READFAULT;
    }

    if (m_startIndex == bodySize)
    {
        // Reached end of buffer
        *pcbRead = 0;
        return S_FALSE;
    }

    size_t bytesWritten = 0;
    try
    {
        hr = readFunction(m_call, m_startIndex, cb, context, static_cast<uint8_t*>(pv), &bytesWritten);
        if (FAILED(hr))
        {
            return STG_E_READFAULT;
        }
    }
    catch (...)
    {
        return STG_E_READFAULT;
    }

    m_startIndex += bytesWritten;

    if (pcbRead != nullptr)
    {
        *pcbRead = static_cast<DWORD>(bytesWritten);
        if (bytesWritten < cb)
        {
            // Reached end of buffer
            return S_FALSE;
        }
    }

    return S_OK;
}
