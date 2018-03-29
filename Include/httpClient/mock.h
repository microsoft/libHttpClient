// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once


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
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCMockCallCreate(
    _Out_ hc_mock_call_handle* call
    ) HC_NOEXCEPT;


/// <summary>
/// Configures libHttpClient to return mock response instead of making a network call 
/// when HCHttpCallPerform() is called. To define a mock response, create a new 
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
/// <param name="requestBodyBytes">
/// If you want the mock to only apply to a specific URL & request string, 
/// pass in a method, URL and a string body.
/// </param>
/// <param name="requestBodySize">
/// The size of requestBodyBytes in bytes.
/// </param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCMockAddMock(
    _In_ hc_mock_call_handle call,
    _In_opt_z_ const_utf8_string method,
    _In_opt_z_ const_utf8_string url,
    _In_reads_bytes_opt_(requestBodySize) const PBYTE requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) HC_NOEXCEPT;

/// <summary>
/// Removes and cleans up all mock calls added by HCMockAddMock
/// </summary>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
STDAPI HCMockClearMocks() HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// HCMockResponse Set APIs
// 

/// <summary>
/// Set the response body string to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="responseString">the response body string of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCMockResponseSetResponseString(
    _In_ hc_mock_call_handle call,
    _In_z_ const_utf8_string responseString
    ) HC_NOEXCEPT;

/// <summary>
/// Set the HTTP status code to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCMockResponseSetStatusCode(
    _In_ hc_mock_call_handle call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT;

/// <summary>
/// Set the network error code to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="networkErrorCode">The network error code of the HTTP call.</param>
/// <param name="platformNetworkErrorCode">The platform specific network error code of the HTTP call to be used for logging / debugging</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCMockResponseSetNetworkErrorCode(
    _In_ hc_mock_call_handle call,
    _In_ HRESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) HC_NOEXCEPT;

/// <summary>
/// Set a response header to return for the mock
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">Response header name for the HTTP call</param>
/// <param name="headerValue">Response header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCMockResponseSetHeader(
    _In_ hc_mock_call_handle call,
    _In_z_ const_utf8_string headerName,
    _In_z_ const_utf8_string headerValue
    ) HC_NOEXCEPT;


