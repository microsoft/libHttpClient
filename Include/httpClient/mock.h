// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
// Mock APIs
// 

/// <summary>
/// Creates a mock HTTP call handle
///
/// First create a HTTP handle using HCMockCallCreate()
/// Then call HCMockResponseSet*() to prepare the HC_MOCK_CALL_HANDLE
/// Then call HCMockAddMock() to add it to the system
/// </summary>
/// <param name="call">The handle of the mock HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockCallCreate(
    _Out_ HC_MOCK_CALL_HANDLE* call
    ) HC_NOEXCEPT;


/// <summary>
/// Configures libHttpClient to return mock response instead of making a network call 
/// when HCHttpCallPerform() is called. To define a mock response, create a new 
/// HC_MOCK_CALL_HANDLE with HCMockCallCreate() that represents the mock.
/// Then use HCMockResponseSet*() to set the mock response.
/// 
/// By default, the mock response will be returned for all HTTP calls.
/// If you want the mock to only apply to a specific URL, pass in a URL. 
/// If you want the mock to only apply to a specific URL & request string, 
/// pass in a URL and a string body.
///
/// Once the HC_MOCK_CALL_HANDLE is configured as desired, add the mock to the system by 
/// calling HCMockAddMock(). 
/// 
/// You can set multiple active mock responses by calling HCMockAddMock() multiple 
/// times with a set of mock responses. If the HTTP call matches against a set mock responses, 
/// they will be executed in order for each subsequent call to HCHttpCallPerform(). When the 
/// last matching mock response is hit, the last matching mock response will be repeated on 
/// each subsequent call to HCHttpCallPerform().
/// </summary>
/// <param name="call">This HC_MOCK_CALL_HANDLE that represents the mock that has been configured 
/// accordingly using HCMockResponseSet*()</param>
/// <param name="method">
/// If you want the mock to only apply to a specific URL, pass in a method and URL. 
/// </param>
/// <param name="url">
/// If you want the mock to only apply to a specific URL, pass in a method and URL. 
/// </param>
/// <param name="requestBodyString">
/// If you want the mock to only apply to a specific URL & request string, 
/// pass in a method, URL and a string body.
/// </param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_opt_z_ PCSTR method,
    _In_opt_z_ PCSTR url,
    _In_opt_z_ PCSTR requestBodyString
    ) HC_NOEXCEPT;

/// <summary>
/// Removes and cleans up all mock calls added by HCMockAddMock
/// </summary>
/// <returns>Result code for this API operation.  Possible values are HC_OK, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockClearMocks() HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// HCMockResponse Set APIs
// 

/// <summary>
/// Set the response body string to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="responseString">the response body string of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetResponseString(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_z_ PCSTR responseString
    ) HC_NOEXCEPT;

/// <summary>
/// Set the HTTP status code to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetStatusCode(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT;

/// <summary>
/// Set the network error code to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="networkErrorCode">The network error code of the HTTP call.</param>
/// <param name="platformNetworkErrorCode">The platform specific network error code of the HTTP call to be used for logging / debugging</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetNetworkErrorCode(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ HC_RESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) HC_NOEXCEPT;

/// <summary>
/// Set a response header to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">Response header name for the HTTP call</param>
/// <param name="headerValue">Response header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetHeader(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_z_ PCSTR headerName,
    _In_z_ PCSTR headerValue
    ) HC_NOEXCEPT;


#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

