// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "mem.h"
#include "singleton.h"
#include "log.h"

HC_MEM_ALLOC_FUNC g_memAllocFunc = nullptr;
HC_MEM_FREE_FUNC g_memFreeFunc = nullptr;

NAMESPACE_XBOX_LIBHCBEGIN

void* http_memory::mem_alloc(
    _In_ size_t dwSize
    )
{
    std::function<_Ret_maybenull_ _Post_writable_byte_size_(dwSize) void*(_In_ size_t dwSize)> pMemAlloc;
    if (pMemAlloc == nullptr)
    {
        return new (std::nothrow) int8_t[dwSize];
    }
    else
    {
        try
        {
            return pMemAlloc(dwSize);
        }
        catch (...)
        {
            LOG_ERROR("mem_alloc callback failed.");
            return nullptr;
        }
    }
}

void http_memory::mem_free(
    _In_ void* pAddress
    )
{
    std::function<void(_In_ void* pAddress)> pMemFree = nullptr;
    auto singleton = get_http_singleton();
    if (singleton != nullptr)
    {
        pMemFree = singleton->m_pMemFreeHook;
    }
    if (pMemFree == nullptr)
    {
        delete[] pAddress;
    }
    else
    {
        try
        {
            return pMemFree(pAddress);
        }
        catch (...)
        {
            LOG_ERROR("mem_free callback failed.");
        }
    }
}

NAMESPACE_XBOX_LIBHCEND


HC_API void HC_CALLING_CONV
HCMemSetFunctions(
    _In_opt_ HC_MEM_ALLOC_FUNC memAllocFunc,
    _In_opt_ HC_MEM_FREE_FUNC memFreeFunc
    )
{
    g_memAllocFunc = memAllocFunc;
    g_memFreeFunc = memFreeFunc;
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
