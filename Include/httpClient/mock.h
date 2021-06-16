// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#if !defined(__cplusplus)
    #error C++11 required
#endif

#pragma once
#include <httpClient/pal.h>

extern "C"
{

/////////////////////////////////////////////////////////////////////////////////////////
// Mock APIs
// 

/// <summary>
/// Creates a mock HTTP call handle.
/// </summary>
/// <param name="call">The handle of the mock HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>
/// First create a HTTP handle using HCMockCallCreate().
/// Then call HCMockResponseSet*() to prepare the HC_MOCK_CALL_HANDLE.
/// Then call HCMockAddMock() to add it to the system.
/// </remarks>
STDAPI HCMockCallCreate(
    _Out_ HCMockCallHandle* call
    ) noexcept;


/// <summary>
/// Configures libHttpClient to return mock response instead of making a network call 
/// when HCHttpCallPerformAsync() is called.
/// </summary>
/// <param name="call">This HC_MOCK_CALL_HANDLE that represents the mock that has been configured 
/// accordingly using HCMockResponseSet*()</param>
/// <param name="method">
/// If you want the mock to only apply to a specific URL, pass in a UTF-8 encoded method and URL. 
/// </param>
/// <param name="url">
/// If you want the mock to only apply to a specific URL, pass in a UTF-8 encoded method and URL. 
/// </param>
/// <param name="requestBodyBytes">
/// If you want the mock to only apply to a specific URL & request string, 
/// pass in a method, URL and a string body.
/// </param>
/// <param name="requestBodySize">
/// The size of requestBodyBytes in bytes.
/// </param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
/// <remarks>
/// To define a mock response, create a new 
/// HC_MOCK_CALL_HANDLE with HCMockCallCreate() that represents the mock.
/// Then use HCMockResponseSet*() to set the mock response.
/// 
/// By default, the mock response will be returned for all HTTP calls.
/// If you want the mock to only apply to a specific URL, pass in a URL. 
/// If you want the mock to only apply to a specific URL & request body, 
/// pass in a URL and a body.
///
/// Once the HC_MOCK_CALL_HANDLE is configured as desired, add the mock to the system by 
/// calling HCMockAddMock(). 
/// 
/// You can set multiple active mock responses by calling HCMockAddMock() multiple 
/// times with a set of mock responses. If the HTTP call matches against a set mock responses, 
/// they will be executed in order for each subsequent call to HCHttpCallPerformAsync(). When the 
/// last matching mock response is hit, the last matching mock response will be repeated on 
/// each subsequent call to HCHttpCallPerformAsync().
/// </remarks>
STDAPI HCMockAddMock(
    _In_ HCMockCallHandle call,
    _In_opt_z_ const char* method,
    _In_opt_z_ const char* url,
    _In_reads_bytes_opt_(requestBodySize) const uint8_t* requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) noexcept;

/// <summary>
/// Callback to be invoked when a mock is matched (before completing the call).
/// </summary>
/// <param name="matchedMock">The matched mock.</param>
/// <param name="method">Method of the original call.</param>
/// <param name="url">Url of the original call.</param>
/// <param name="requestBodyBytes">Request body from the original call.</param>
/// <param name="requestBodySize">Size of the request body.</param>
/// <param name="context">Client context.</param>
typedef void (CALLBACK* HCMockMatchedCallback)(
    _In_ HCMockCallHandle matchedMock,
    _In_ const char* method,
    _In_ const char* url,
    _In_ const uint8_t* requestBodyBytes,
    _In_ uint32_t requestBodySize,
    _In_ void* context
    );

/// <summary>
/// Add an intermediate callback that will be called when the specified mock is matched.
/// This gives the client another opportunity to specify the mock response.
/// </summary>
/// <param name="call">The matched mock.</param>
/// <param name="callback">Callback to be invoked when the mock is matched.</param>
/// <param name="context">Client context.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCMockSetMockMatchedCallback(
    _In_ HCMockCallHandle call,
    _In_ HCMockMatchedCallback callback,
    _In_opt_ void* context
    );

/// <summary>
/// Removes and cleans up the mock.
/// </summary>
/// <param name="call">The matched mock.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
STDAPI HCMockRemoveMock(
    _In_ HCMockCallHandle call
    );

/// <summary>
/// Removes and cleans up all mock calls added by HCMockAddMock.
/// </summary>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
STDAPI HCMockClearMocks() noexcept;


/////////////////////////////////////////////////////////////////////////////////////////
// HCMockResponse Set APIs
// 

/// <summary>
/// Set the response body string to return for the mock.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="bodyBytes">The response body bytes of the HTTP call.</param>
/// <param name="bodySize">The length in bytes of the body being set.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCMockResponseSetResponseBodyBytes(
    _In_ HCMockCallHandle call,
    _In_reads_bytes_(bodySize) const uint8_t* bodyBytes,
    _In_ uint32_t bodySize
    ) noexcept;

/// <summary>
/// Set the HTTP status code to return for the mock.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCMockResponseSetStatusCode(
    _In_ HCMockCallHandle call,
    _In_ uint32_t statusCode
    ) noexcept;

/// <summary>
/// Set the network error code to return for the mock.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="networkErrorCode">The network error code of the HTTP call.</param>
/// <param name="platformNetworkErrorCode">The platform specific network error code of the HTTP call to be used for logging / debugging.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCMockResponseSetNetworkErrorCode(
    _In_ HCMockCallHandle call,
    _In_ HRESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) noexcept;

/// <summary>
/// Set a response header to return for the mock.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="headerName">UTF-8 encoded response header name for the HTTP call.</param>
/// <param name="headerValue">UTF-8 encoded response header value for the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCMockResponseSetHeader(
    _In_ HCMockCallHandle call,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
    ) noexcept;

}
