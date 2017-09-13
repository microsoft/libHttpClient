// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include <stdint.h>
#include <httpClient/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

    /// <summary>
/// The callback definition used by HCGlobalSetHttpCallPerformFunction().
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="taskHandle">The handle to the task</param>
typedef void
(HC_CALLING_CONV* HC_HTTP_CALL_PERFORM_FUNC)(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    );

/// <summary>
/// Optionally allows the caller to implement the HTTP perform function.
/// In the HC_HTTP_CALL_PERFORM_FUNC callback, use HCHttpCallRequestGet*() and HCSettingsGet*() to 
/// get information about the HTTP call and perform the call as desired and set 
/// the response with HCHttpCallResponseSet*().
/// </summary>
/// <param name="performFunc">A callback where that implements HTTP perform function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform</param>
HC_API void HC_CALLING_CONV
HCGlobalSetHttpCallPerformFunction(
    _In_opt_ HC_HTTP_CALL_PERFORM_FUNC performFunc
    );

/// <summary>
/// Returns the current HC_HTTP_CALL_PERFORM_FUNC callback which implements the HTTP 
/// perform function on the current platform. This can be used along with 
/// HCGlobalSetHttpCallPerformFunction() to monitor all HTTP calls.
/// </summary>
/// <param name="performFunc">Set to the current HTTP perform function. Returns the default 
/// routine if not previously set</param>
HC_API void HC_CALLING_CONV
HCGlobalGetHttpCallPerformFunction(
    _Out_ HC_HTTP_CALL_PERFORM_FUNC* performFunc
    );


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallRequest Get APIs
//

/// <summary>
/// Gets the url and method for the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="method">Method for the HTTP call</param>
/// <param name="url">URL for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetUrl(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR* method,
    _Outptr_ PCSTR* url
    );

/// <summary>
/// Get the request body string of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="requestBodyString">the request body string of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR* requestBodyString
    );

/// <summary>
/// Get a request header for the HTTP call for a given header name
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">request header name for the HTTP call</param>
/// <param name="headerValue">request header value for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    );

/// <summary>
/// Gets the number of request headers in the the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="numHeaders">the number of request headers in the the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    );

/// <summary>
/// Gets the request headers at specific zero based index in the the HTTP call.
/// Use HCHttpCallRequestGetNumHeaders() to know how many request headers there are in the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerIndex">Specific zero based index of the request header</param>
/// <param name="headerName">Request header name for the HTTP call</param>
/// <param name="headerValue">Request header value for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    );

/// <summary>
/// Gets if retry is allowed for this HTTP call
/// Defaults to true 
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="retryAllowed">If retry is allowed for this HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ bool* retryAllowed
    );

/// <summary>
/// Gets the timeout for this HTTP call.
/// Defaults to 30 seconds
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls</param>
/// <param name="timeoutInSeconds">the timeout for this HTTP call.</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutInSeconds
    );

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
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t* retryDelayInSeconds
    );

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
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutWindowInSeconds
    );

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
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetAssertsForThrottling(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ bool* enableAssertsForThrottling
    );


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse Set APIs
// 

/// <summary>
/// Set the response body string of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="responseString">the response body string of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetResponseString(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ PCSTR responseString
    );

/// <summary>
/// Set the HTTP status code of the HTTP call response
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetStatusCode(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t statusCode
    );

/// <summary>
/// Set the network error code of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="errorCode">the network error code of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetErrorCode(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t errorCode
    );

/// <summary>
/// Set a response header for the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">Response header name for the HTTP call</param>
/// <param name="headerValue">Response header value for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _In_ PCSTR headerValue
    );

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

