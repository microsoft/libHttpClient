// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../Global/global.h"

_Ret_maybenull_ _Post_writable_byte_size_(size) void* STDAPIVCALLTYPE 
DefaultMemAllocFunction(
    _In_ size_t size,
    _In_ HCMemoryType memoryType
    )
{
    UNREFERENCED_PARAMETER(memoryType);
    return malloc(size);
}

void STDAPIVCALLTYPE 
DefaultMemFreeFunction(
    _In_ _Post_invalid_ void* pointer,
    _In_ HCMemoryType memoryType
    )
{
    UNREFERENCED_PARAMETER(memoryType);
    free(pointer);
}

HCMemAllocFunction g_memAllocFunc = DefaultMemAllocFunction;
HCMemFreeFunction g_memFreeFunc = DefaultMemFreeFunction;

STDAPI 
HCMemSetFunctions(
    _In_opt_ HCMemAllocFunction memAllocFunc,
    _In_opt_ HCMemFreeFunction memFreeFunc
    ) noexcept
{
    if (xbox::httpclient::get_http_singleton() != nullptr)
    {
        return E_HC_ALREADY_INITIALISED;
    }

    g_memAllocFunc = (memAllocFunc == nullptr) ? DefaultMemAllocFunction : memAllocFunc;
    g_memFreeFunc = (memFreeFunc == nullptr) ? DefaultMemFreeFunction : memFreeFunc;
    return S_OK;
}

STDAPI 
HCMemGetFunctions(
    _Out_ HCMemAllocFunction* memAllocFunc,
    _Out_ HCMemFreeFunction* memFreeFunc
    ) noexcept
{
    if (memAllocFunc == nullptr || memFreeFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    *memAllocFunc = g_memAllocFunc;
    *memFreeFunc = g_memFreeFunc;
    return S_OK;
}


NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

_Ret_maybenull_ _Post_writable_byte_size_(size)
void* http_memory::mem_alloc(
    _In_ size_t size
    )
{
    HCMemAllocFunction pMemAlloc = g_memAllocFunc;
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
    HCMemFreeFunction pMemFree = g_memFreeFunc;
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

