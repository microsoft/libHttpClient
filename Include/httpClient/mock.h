// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "types.h"
#include "task.h"
#include "httpProvider.h"

#if defined(__cplusplus)
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
// Mock APIs
// 

/// <summary>
/// Configures libHttpClient to return mock response instead of making a network call 
/// when HCHttpCallPerform() is called. To define a mock response, create a new 
/// HC_CALL_HANDLE with HCHttpCallCreate() that represents the mock.
/// Then use HCHttpCallResponseSet*() to set the mock response.
/// 
/// By default, the mock response will be returned for all HTTP calls.
/// If you want the mock to only apply to a specific URL, call 
/// HCHttpCallRequestSetUrl() with the HC_CALL_HANDLE that represents the mock.
/// If you want the mock to only apply to a specific URL & request string, call 
/// HCHttpCallRequestSetUrl() and HCHttpCallRequestSetRequestBodyString() with the 
/// HC_CALL_HANDLE that represents the mock.
///
/// Once the HC_CALL_HANDLE is configured as desired, add the mock to the system by 
/// calling HCMockAddMock(). You do not need to call HCHttpCallCleanup() for 
/// the HC_CALL_HANDLEs passed to HCMockAddMock().
/// 
/// You can set multiple active mock responses by calling HCMockAddMock() multiple 
/// times with a set of mock responses. If the HTTP call matches against a set mock responses, 
/// they will be executed in order for each subsequent call to HCHttpCallPerform(). When the 
/// last matching mock response is hit, the last matching mock response will be repeated on 
/// each subsequent call to HCHttpCallPerform().
/// </summary>
/// <param name="call">This HC_CALL_HANDLE that represents the mock that has been configured 
/// accordingly using HCHttpCallResponseSet*(), and optionally HCHttpCallRequestSetUrl() 
/// and HCHttpCallRequestSetRequestBodyString().</param>
HC_API void HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_CALL_HANDLE call
    );

/// <summary>
/// Removes and cleans up all mock calls added by HCMockAddMock
/// </summary>
HC_API void HC_CALLING_CONV
HCMockClearMocks();

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

