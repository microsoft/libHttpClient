// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"

class http_buffer
{
public:
    void append(
        _In_reads_bytes_(cb) const void* pv,
        _In_ ULONG cb
        );

    http_internal_string as_string();
    http_internal_vector<BYTE>& as_buffer() { return m_buffer; }

private:
    http_internal_vector<BYTE> m_buffer;
};
