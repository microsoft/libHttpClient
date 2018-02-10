// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class http_buffer
{
public:
    void append(
        _In_reads_bytes_(cb) const void* pv,
        _In_ ULONG cb
        );

    http_internal_string as_string();
    size_t size() { return m_buffer.size(); }

private:
    http_internal_vector<BYTE> m_buffer;
};

NAMESPACE_XBOX_HTTP_CLIENT_END