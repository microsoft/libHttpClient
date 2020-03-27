// Copyright(c) Microsoft Corporation. All rights reserved.
//
// These APIs should be reserved for driving unit test harnesses.

#pragma once

#include "XAsyncProvider.h"

/// <summary>
/// Initializes an async block for use.  Once begun calls such
/// as XAsyncGetStatus will provide meaningful data. It is assumed the
/// async work will begin on some system defined thread after this call
/// returns. The token and function parameters can be used to help identify
/// mismatched Begin/GetResult calls.  The token is typically the function
/// pointer of the async API you are implementing, and the functionName parameter
/// is typically the __FUNCTION__ compiler macro.  
///
/// This variant of XAsyncBegin will allocate additional memory of size contextSize
/// and use this as the context pointer for async provider callbacks.  The memory
/// pointer is returned in 'context'.  The lifetime of this memory is managed
/// by the async library and will be freed automatically when the call 
/// completes.
/// </summary>
/// <param name='asyncBlock'>A pointer to the XAsyncBlock that holds data for the call.</param>
/// <param name='identity'>An optional arbitrary pointer that can be used to identify this call.</param>
/// <param name='identityName'>An optional string that names the async call.  This is typically the __FUNCTION__ compiler macro.</param>
/// <param name='provider'>The function callback to invoke to implement the async call.</param>
/// <param name='contextSize'>The size, in bytes, of additional context memory to allocate.</param>
/// <param name='parameterBlockSize'>The size of a parameter block to copy into the allocated context.</param>
/// <param name='parameterBlock'>The parameter block to copy.  This will be copied into the allocated context.</param>
STDAPI XAsyncBeginAlloc(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ const void* identity,
    _In_opt_ const char* identityName,
    _In_ XAsyncProvider* provider,
    _In_ size_t contextSize,
    _In_ size_t parameterBlockSize,
    _In_opt_ void* parameterBlock
    ) noexcept;
