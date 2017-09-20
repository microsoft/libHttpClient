// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

_Ret_maybenull_ _Post_writable_byte_size_(size) void* HC_CALLING_CONV 
DefaultMemAllocFunction(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    )
{
    return malloc(size);
}

void HC_CALLING_CONV 
DefaultMemFreeFunction(
    _In_ _Post_invalid_ void* pointer,
    _In_ HC_MEMORY_TYPE memoryType
    )
{
    free(pointer);
}

HC_MEM_ALLOC_FUNC g_memAllocFunc = DefaultMemAllocFunction;
HC_MEM_FREE_FUNC g_memFreeFunc = DefaultMemFreeFunction;

HC_API void HC_CALLING_CONV
HCMemSetFunctions(
    _In_opt_ HC_MEM_ALLOC_FUNC memAllocFunc,
    _In_opt_ HC_MEM_FREE_FUNC memFreeFunc
    ) HC_NOEXCEPT
{
    g_memAllocFunc = (memAllocFunc == nullptr) ? DefaultMemAllocFunction : memAllocFunc;
    g_memFreeFunc = (memFreeFunc == nullptr) ? DefaultMemFreeFunction : memFreeFunc;
}

HC_API HC_RESULT HC_CALLING_CONV
HCMemGetFunctions(
    _Out_ HC_MEM_ALLOC_FUNC* memAllocFunc,
    _Out_ HC_MEM_FREE_FUNC* memFreeFunc
    ) HC_NOEXCEPT
{
    if (memAllocFunc == nullptr || memFreeFunc == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *memAllocFunc = g_memAllocFunc;
    *memFreeFunc = g_memFreeFunc;
    return HC_OK;
}


NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

_Ret_maybenull_ _Post_writable_byte_size_(size)
void* http_memory::mem_alloc(
    _In_ size_t size
    )
{
    HC_MEM_ALLOC_FUNC pMemAlloc = g_memAllocFunc;
    try
    {
        return pMemAlloc(size, 0);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "mem_alloc callback failed");
        return nullptr;
    }
}

void http_memory::mem_free(
    _In_opt_ void* pAddress
    )
{
    HC_MEM_FREE_FUNC pMemFree = g_memFreeFunc;
    try
    {
        if (pAddress)
        {
            return pMemFree(pAddress, 0);
        }
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "mem_free callback failed");
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_END

