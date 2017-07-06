// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "mem.h"
#include "singleton.h"
#include "log.h"

_Ret_maybenull_ _Post_writable_byte_size_(size) void* HC_CALLING_CONV 
DefaultMemAllocFunction(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    )
{
    return new (std::nothrow) int8_t[size];
}

void HC_CALLING_CONV 
DefaultMemFreeFunction(
    _In_ _Post_invalid_ void* pointer,
    _In_ HC_MEMORY_TYPE memoryType
    )
{
    delete[] pointer;
}


HC_MEM_ALLOC_FUNC g_memAllocFunc = DefaultMemAllocFunction;
HC_MEM_FREE_FUNC g_memFreeFunc = DefaultMemFreeFunction;

NAMESPACE_XBOX_LIBHCBEGIN


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
        LOG_ERROR("mem_alloc callback failed.");
        return nullptr;
    }
}

void http_memory::mem_free(
    _In_ void* pAddress
    )
{
    HC_MEM_FREE_FUNC pMemFree = g_memFreeFunc;
    try
    {
        return pMemFree(pAddress, 0);
    }
    catch (...)
    {
        LOG_ERROR("mem_free callback failed.");
    }
}

NAMESPACE_XBOX_LIBHCEND


HC_API void HC_CALLING_CONV
HCMemSetFunctions(
    _In_opt_ HC_MEM_ALLOC_FUNC memAllocFunc,
    _In_opt_ HC_MEM_FREE_FUNC memFreeFunc
    )
{
    g_memAllocFunc = (memAllocFunc == nullptr) ? DefaultMemAllocFunction : memAllocFunc;
    g_memFreeFunc = (memFreeFunc == nullptr) ? DefaultMemFreeFunction : memFreeFunc;
}

HC_API void HC_CALLING_CONV
HCMemGetFunctions(
    _Out_ HC_MEM_ALLOC_FUNC* memAllocFunc,
    _Out_ HC_MEM_FREE_FUNC* memFreeFunc
    )
{
    assert(memAllocFunc != nullptr);
    assert(memFreeFunc != nullptr);
    *memAllocFunc = g_memAllocFunc;
    *memFreeFunc = g_memFreeFunc;
}
