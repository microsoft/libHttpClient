// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include <httpClient/pal.h>
#include <httpClient/asyncProvider.h>
#include <httpClient/trace.h>


    /// <summary>
/// The callback definition used by HCGlobalSetHttpCallPerformFunction().
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="taskHandle">The handle to the task</param>
typedef void
(HC_CALLING_CONV* HCCallPerformFunction)(
    _In_ hc_call_handle call,
    _In_ AsyncBlock* asyncBlock
    );

/// <summary>
/// Optionally allows the caller to implement the HTTP perform function.
/// In the HCCallPerformFunction callback, use HCHttpCallRequestGet*() and HCSettingsGet*() to 
/// get information about the HTTP call and perform the call as desired and set 
/// the response with HCHttpCallResponseSet*().
/// </summary>
/// <param name="performFunc">A callback that implements HTTP perform function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
HCAPI_(void) HCGlobalSetHttpCallPerformFunction(
    _In_opt_ HCCallPerformFunction performFunc
    ) HC_NOEXCEPT;

/// <summary>
/// Returns the current HCCallPerformFunction callback which implements the HTTP 
/// perform function on the current platform. This can be used along with 
/// HCGlobalSetHttpCallPerformFunction() to monitor all HTTP calls.
/// </summary>
/// <param name="performFunc">Set to the current HTTP perform function. Returns the default 
/// routine if not previously set</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCGlobalGetHttpCallPerformFunction(
    _Out_ HCCallPerformFunction* performFunc
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the context pointer attached to this call object
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="context">the context pointer attached to this call object</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallGetContext(
    _In_ hc_call_handle call,
    _In_ void** context
    ) HC_NOEXCEPT;

/// <summary>
/// Sets the context pointer attached to this call object
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="context">the context pointer attached to this call object</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallSetContext(
    _In_ hc_call_handle call,
    _In_ void* context
    ) HC_NOEXCEPT;

/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallRequest Get APIs
//

/// <summary>
/// Gets the url and method for the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="method">Method for the HTTP call</param>
/// <param name="url">URL for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetUrl(
    _In_ hc_call_handle call,
    _Outptr_ const_utf8_string* method,
    _Outptr_ const_utf8_string* url
    ) HC_NOEXCEPT;

/// <summary>
/// Get the request body bytes of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="requestBodyBytes">The request body bytes of the HTTP call</param>
/// <param name="requestBodySize">The request body bytes size in bytes of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetRequestBodyBytes(
    _In_ hc_call_handle call,
    _Outptr_result_bytebuffer_maybenull_(*requestBodySize) const BYTE** requestBodyBytes,
    _Out_ uint32_t* requestBodySize
    ) HC_NOEXCEPT;

/// <summary>
/// Get the request body bytes of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="requestBody">The request body of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetRequestBodyString(
    _In_ hc_call_handle call,
    _Outptr_ const_utf8_string* requestBody
    ) HC_NOEXCEPT;

/// <summary>
/// Get a request header for the HTTP call for a given header name
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">request header name for the HTTP call</param>
/// <param name="headerValue">request header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetHeader(
    _In_ hc_call_handle call,
    _In_z_ const_utf8_string headerName,
    _Out_ const_utf8_string* headerValue
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the number of request headers in the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="numHeaders">the number of request headers in the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetNumHeaders(
    _In_ hc_call_handle call,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the request headers at specific zero based index in the HTTP call.
/// Use HCHttpCallRequestGetNumHeaders() to know how many request headers there are in the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerIndex">Specific zero based index of the request header</param>
/// <param name="headerName">Request header name for the HTTP call</param>
/// <param name="headerValue">Request header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetHeaderAtIndex(
    _In_ hc_call_handle call,
    _In_ uint32_t headerIndex,
    _Out_ const_utf8_string* headerName,
    _Out_ const_utf8_string* headerValue
    ) HC_NOEXCEPT;

/// <summary>
/// Gets if retry is allowed for this HTTP call
/// Defaults to true 
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="retryAllowed">If retry is allowed for this HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetRetryAllowed(
    _In_opt_ hc_call_handle call,
    _Out_ bool* retryAllowed
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the ID number of this REST endpoint used to cache the Retry-After header for fast fail.
/// Defaults is 0
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="retryAfterCacheId">ID number of this REST endpoint used to cache the Retry-After header for fast fail.  1-1000 are reserved for XSAPI</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetRetryCacheId(
    _In_ hc_call_handle call,
    _Out_ uint32_t* retryAfterCacheId
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the timeout for this HTTP call.
/// Defaults to 30 seconds
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="timeoutInSeconds">the timeout for this HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetTimeout(
    _In_opt_ hc_call_handle call,
    _Out_ uint32_t* timeoutInSeconds
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the HTTP retry delay in seconds. The default and minimum delay is 2 seconds.
/// 
/// Retries are delayed using a exponential back off.  By default, it will delay 2 seconds then the 
/// next retry will delay 4 seconds, then 8 seconds, and so on up to a max of 1 min until either
/// the call succeeds or the HTTP timeout window is reached, at which point the call will fail.
/// The delay is also jittered between the current and next delay to spread out service load.
/// The default for the HTTP timeout window is 20 seconds and can be changed using HCSettingsSetTimeoutWindow()
/// 
/// If the service returns an an HTTP error with a "Retry-After" header, then all future calls to that API 
/// will immediately fail with the original error without contacting the service until the "Retry-After" 
/// time has been reached.
///
/// Idempotent service calls are retried when a network error occurs or the server responds 
/// with one of these HTTP status codes:
/// 408 (Request Timeout)
/// 429 (Too Many Requests)
/// 500 (Internal Server Error)
/// 502 (Bad Gateway)
/// 503 (Service Unavailable)
/// 504 (Gateway Timeout)
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="retryDelayInSeconds">The retry delay in seconds</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetRetryDelay(
    _In_opt_ hc_call_handle call,
    _In_ uint32_t* retryDelayInSeconds
    ) HC_NOEXCEPT;

/// <summary>
/// Sets the HTTP timeout window in seconds.
///
/// This controls how long to spend attempting to retry idempotent service calls before failing.
/// The default is 20 seconds
///
/// Idempotent service calls are retried when a network error occurs or the server responds 
/// with one of these HTTP status codes:
/// 408 (Request Timeout)
/// 429 (Too Many Requests)
/// 500 (Internal Server Error)
/// 502 (Bad Gateway)
/// 503 (Service Unavailable)
/// 504 (Gateway Timeout)
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="timeoutWindowInSeconds">The timeout window in seconds</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetTimeoutWindow(
    _In_opt_ hc_call_handle call,
    _Out_ uint32_t* timeoutWindowInSeconds
    ) HC_NOEXCEPT;

/// <summary>
/// Gets if assert are enabled if throttled.
/// This means if HTTP status 429 is returned, a debug assert is triggered.
/// This causes caller to immediately notice that their calling pattern is too fast and should be corrected.
///
/// It is best practice to not call this API, and instead adjust the calling pattern but this is provided
/// as a temporary way to get unblocked while in early stages of game development.
///
/// Default is true.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="enableAssertsForThrottling">True if assert are enabled if throttled</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallRequestGetAssertsForThrottling(
    _In_opt_ hc_call_handle call,
    _Out_ bool* enableAssertsForThrottling
    ) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse Set APIs
// 

/// <summary>
/// Set the response body string of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="responseString">the response body string of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
HCAPI HCHttpCallResponseSetResponseString(
    _In_ hc_call_handle call,
    _In_z_ const_utf8_string responseString
    ) HC_NOEXCEPT;

/// <summary>
/// Set the HTTP status code of the HTTP call response
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallResponseSetStatusCode(
    _In_ hc_call_handle call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT;

/// <summary>
/// Set the network error code of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="networkErrorCode">The network error code of the HTTP call.</param>
/// <param name="platformNetworkErrorCode">The platform specific network error code of the HTTP call to be used for logging / debugging</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI HCHttpCallResponseSetNetworkErrorCode(
    _In_ hc_call_handle call,
    _In_ HRESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) HC_NOEXCEPT;

/// <summary>
/// Set a response header for the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">Response header name for the HTTP call</param>
/// <param name="headerValue">Response header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
HCAPI HCHttpCallResponseSetHeader(
    _In_ hc_call_handle call,
    _In_z_ const_utf8_string headerName,
    _In_z_ const_utf8_string headerValue
    ) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// WebSocket Set APIs
// 

/// <summary>
/// Function to connects to the WebSocket.  This API returns immediately and will spin up a thread under the covers.
/// On UWP and XDK, the thread is owned and controlled by Windows::Networking::Sockets::MessageWebSocket
/// </summary>
/// <param name="uri">The URI to connect to</param>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="args">Struct for describing the WebSocket connection args</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
typedef HRESULT
(HC_CALLING_CONV* HCWebSocketConnectFunction)(
    _In_z_ const_utf8_string uri,
    _In_z_ const_utf8_string subProtocol,
    _In_ hc_websocket_handle websocket,
    _In_ AsyncBlock* async
    );

/// <summary>
/// Send message the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
typedef HRESULT
(HC_CALLING_CONV* HCWebSocketSendMessageFunction)(
    _In_ hc_websocket_handle websocket,
    _In_z_ const_utf8_string message,
    _In_ AsyncBlock* async
    );

/// <summary>
/// Closes the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
typedef HRESULT
(HC_CALLING_CONV* HCWebSocketDisconnectFunction)(
    _In_ hc_websocket_handle websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    );

/// <summary>
/// Optionally allows the caller to implement the WebSocket functions.
/// </summary>
/// <param name="websocketConnectFunc">A callback that implements WebSocket connect function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <param name="websocketSendMessageFunc">A callback that implements WebSocket send message function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <param name="websocketDisconnectFunc">A callback that implements WebSocket disconnect function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
HCAPI 
HCGlobalSetWebSocketFunctions(
    _In_opt_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_opt_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_opt_ HCWebSocketDisconnectFunction websocketDisconnectFunc
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the functions that implement the WebSocket functions.
/// </summary>
/// <param name="websocketConnectFunc">A callback that implements WebSocket connect function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <param name="websocketSendMessageFunc">A callback that implements WebSocket send message function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <param name="websocketDisconnectFunc">A callback that implements WebSocket disconnect function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
HCAPI 
HCGlobalGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc
    ) HC_NOEXCEPT;

/// <summary>
/// Get the proxy URI for the WebSocket
/// </summary>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="proxyUri">The proxy URI for the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
HCAPI 
HCWebSocketGetProxyUri(
    _In_ hc_websocket_handle websocket,
    _Out_ const_utf8_string* proxyUri
    ) HC_NOEXCEPT;

/// <summary>
/// Get a header for the WebSocket
/// </summary>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="headerName">Header name for the WebSocket</param>
/// <param name="headerValue">Header value for the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
HCAPI 
HCWebSocketGetHeader(
    _In_ hc_websocket_handle websocket,
    _In_z_ const_utf8_string headerName,
    _Out_ const_utf8_string* headerValue
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the number of headers in the WebSocket
/// </summary>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="numHeaders">the number of headers in the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI 
HCWebSocketGetNumHeaders(
    _In_ hc_websocket_handle websocket,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the headers at specific zero based index in the WebSocket.
/// Use HCHttpCallGetNumHeaders() to know how many headers there are in the HTTP call.
/// </summary>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="headerIndex">Specific zero based index of the header</param>
/// <param name="headerName">Header name for the HTTP call</param>
/// <param name="headerValue">Header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
HCAPI 
HCWebSocketGetHeaderAtIndex(
    _In_ hc_websocket_handle websocket,
    _In_ uint32_t headerIndex,
    _Out_ const_utf8_string* headerName,
    _Out_ const_utf8_string* headerValue
) HC_NOEXCEPT;

/// <summary>
/// Gets the WebSocket functions to allow callers to respond to incoming messages and WebSocket close events.
/// </summary>
/// <param name="messageFunc">A pointer to the message handling callback to use, or a null pointer to remove.</param>
/// <param name="closeFunc">A pointer to the close callback to use, or a null pointer to remove.</param>
HCAPI HCWebSocketGetFunctions(
    _Out_opt_ HCWebSocketMessageFunction* messageFunc,
    _Out_opt_ HCWebSocketCloseEventFunction* closeFunc
    ) HC_NOEXCEPT;

