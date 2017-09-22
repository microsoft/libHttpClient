// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "http_buffer.h"

void http_buffer::append(
    _In_reads_bytes_(cb) const void* pv,
    _In_ ULONG cb
    )
{
    const BYTE* bv = reinterpret_cast<const BYTE*>(pv);
    m_buffer.insert(m_buffer.end(), bv, bv + cb);
}

http_internal_string http_buffer::as_string()
{
    return http_internal_string(m_buffer.begin(), m_buffer.end());
}

