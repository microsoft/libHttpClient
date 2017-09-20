// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_BEGIN

HC_RESULT StdBadAllocToResult(std::bad_alloc const& e, _In_z_ char const* file, uint32_t line)
{
    HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::bad_alloc reached api boundary: %s\n    %s:%u",
        HC_E_OUTOFMEMORY, e.what(), file, line);
    return HC_E_OUTOFMEMORY;
}

HC_RESULT StdExceptionToResult(std::exception const& e, _In_z_ char const* file, uint32_t line)
{
    HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception reached api boundary: %s\n    %s:%u",
        HC_E_FAIL, e.what(), file, line);

    HC_ASSERT(false);
    return HC_E_FAIL;
}

HC_RESULT UnknownExceptionToResult(_In_z_ char const* file, uint32_t line)
{
    HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception reached api boundary\n    %s:%u",
        HC_E_FAIL, file, line);

    HC_ASSERT(false);
    return HC_E_FAIL;
}

NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_END