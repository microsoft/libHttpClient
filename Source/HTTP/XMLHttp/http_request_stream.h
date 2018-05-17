// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "../httpcall.h"
#include "xmlhttp_http_task.h"

class http_request_stream : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ISequentialStream>
{
public:
    http_request_stream();

    HRESULT init(
        _In_reads_bytes_(requestBodyBytes) const BYTE* requestBody,
        _In_ uint32_t requestBodyBytes
        );

    virtual HRESULT STDMETHODCALLTYPE Write(
        _In_reads_bytes_(cb) const void *pv,
        _In_ ULONG cb,
        _Out_opt_ ULONG *pcbWritten
        );

    virtual HRESULT STDMETHODCALLTYPE Read(
        _Out_writes_bytes_to_(cb, *pcbRead) void *pv, 
        _In_ ULONG cb, 
        _Out_ ULONG *pcbRead
        );

private:
    std::vector<BYTE> m_requestBody;
    size_t m_remainingToRead;
    size_t m_startIndex;
};

