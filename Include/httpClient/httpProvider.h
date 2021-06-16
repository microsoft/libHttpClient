// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#if !defined(__cplusplus)
    #error C++11 required
#endif

#pragma once

#include <httpClient/async.h>
#include <httpClient/pal.h>
#include <httpClient/trace.h>
#include <httpClient/httpClient.h>

extern "C"
{

/// <summary>
/// The callback definition used by HCSetHttpCallPerformFunction().
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="asyncBlock">The asyncBlock of the async task.</param>
/// <param name="context">The context registered with the callback.</param>
/// <param name="env">The environment for the default callback.</param>
/// <remarks>
/// env is opaque to the client. If the client hooks are filters (they process
/// the request and then pass it on to the default libHttpClient hooks) env
/// should be passed on, otherwise it can be ignored.
/// </remarks>
typedef void
(CALLBACK* HCCallPerformFunction)(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    );

/// <summary>
/// Optionally allows the caller to implement the HTTP perform function.
/// </summary>
/// <param name="performFunc">A callback that implements HTTP perform function as desired.</param>
/// <param name="performContext">The context for the callback.</param>
/// <remarks>
/// Must be called before HCInit.
///
/// In the HCCallPerformFunction callback, use HCHttpCallRequestGet*() and HCSettingsGet*() to 
/// get information about the HTTP call and perform the call as desired and set 
/// the response with HCHttpCallResponseSet*().
/// </remarks>
/// <returns>Result code for this API operation.  Possible values are S_OK, or
/// E_HC_ALREADY_INITIALISED.</returns>
STDAPI HCSetHttpCallPerformFunction(
    _In_ HCCallPerformFunction performFunc,
    _In_opt_ void* performContext
    ) noexcept;

/// <summary>
/// Returns the current HCCallPerformFunction callback which implements the HTTP 
/// perform function on the current platform.
/// </summary>
/// <param name="performFunc">Set to the current HTTP perform function. Returns the default 
/// routine if not previously set.</param>
/// <param name="performContext">The context for the callback.</param>
/// <remarks>
/// This can be used along with HCSetHttpCallPerformFunction() to build a filter that
/// monitors and modifies all HTTP calls, while still using the default HTTP implementation.
/// </remarks>
/// <returns>Result code for this API operation.  Possible values are S_OK,
/// E_HC_ALREADY_INITIALISED, or E_INVALIDARG.</returns>
STDAPI HCGetHttpCallPerformFunction(
    _Out_ HCCallPerformFunction* performFunc,
    _Out_ void** performContext
    ) noexcept;

/// <summary>
/// Gets the context pointer attached to this call object.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="context">the context pointer attached to this call object.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallGetContext(
    _In_ HCCallHandle call,
    _In_ void** context
    ) noexcept;

/// <summary>
/// Sets the context pointer attached to this call object.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="context">the context pointer attached to this call object.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallSetContext(
    _In_ HCCallHandle call,
    _In_opt_ void* context
    ) noexcept;


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallRequest Get APIs
//

/// <summary>
/// Gets the url and method for the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="method">UTF-8 encoded method for the HTTP call.</param>
/// <param name="url">UTF-8 encoded URL for the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallRequestGetUrl(
    _In_ HCCallHandle call,
    _Outptr_ const char** method,
    _Outptr_ const char** url
    ) noexcept;

/// <summary>
/// Get the request body bytes of the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="requestBodyBytes">The request body bytes of the HTTP call.</param>
/// <param name="requestBodySize">The request body bytes size in bytes of the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallRequestGetRequestBodyBytes(
    _In_ HCCallHandle call,
    _Outptr_result_bytebuffer_maybenull_(*requestBodySize) const uint8_t** requestBodyBytes,
    _Out_ uint32_t* requestBodySize
    ) noexcept;

/// <summary>
/// Get the request body bytes of the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="requestBody">The UTF-8 encoded request body of the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallRequestGetRequestBodyString(
    _In_ HCCallHandle call,
    _Outptr_ const char** requestBody
    ) noexcept;

/// <summary>
/// Get the function used by the HTTP call to read the request body
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="readFunction">The read function of this HTTP call.</param>
/// <param name="bodySize">The size of the body.</param>
/// <param name="context">The context associated with this read function.</param>
/// <returns>Result code for this API operation. Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallRequestGetRequestBodyReadFunction(
    _In_ HCCallHandle call,
    _Out_ HCHttpCallRequestBodyReadFunction* readFunction,
    _Out_ size_t* bodySize,
    _Out_ void** context
    ) noexcept;

/// <summary>
/// Get a request header for the HTTP call for a given header name.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">UTF-8 encoded request header name for the HTTP call.</param>
/// <param name="headerValue">UTF-8 encoded request header value for the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallRequestGetHeader(
    _In_ HCCallHandle call,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
    ) noexcept;

/// <summary>
/// Gets the number of request headers in the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="numHeaders">the number of request headers in the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallRequestGetNumHeaders(
    _In_ HCCallHandle call,
    _Out_ uint32_t* numHeaders
    ) noexcept;

/// <summary>
/// Gets the request headers at specific zero based index in the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="headerIndex">Specific zero based index of the request header.</param>
/// <param name="headerName">UTF-8 encoded request header name for the HTTP call.</param>
/// <param name="headerValue">UTF-8 encoded request header value for the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>Use HCHttpCallRequestGetNumHeaders() to know how many request headers there are in the HTTP call.</remarks>
STDAPI HCHttpCallRequestGetHeaderAtIndex(
    _In_ HCCallHandle call,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
    ) noexcept;

/// <summary>
/// Gets if retry is allowed for this HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls.</param>
/// <param name="retryAllowed">If retry is allowed for this HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>Defaults to true.</remarks>
STDAPI HCHttpCallRequestGetRetryAllowed(
    _In_opt_ HCCallHandle call,
    _Out_ bool* retryAllowed
    ) noexcept;

/// <summary>
/// Gets the ID number of this REST endpoint used to cache the Retry-After header for fast fail.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls.</param>
/// <param name="retryAfterCacheId">ID number of this REST endpoint used to cache the Retry-After header for fast fail.  1-1000 are reserved for XSAPI.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>Defaults is 0.</remarks>
STDAPI HCHttpCallRequestGetRetryCacheId(
    _In_ HCCallHandle call,
    _Out_ uint32_t* retryAfterCacheId
    ) noexcept;

/// <summary>
/// Gets the timeout for this HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls.</param>
/// <param name="timeoutInSeconds">the timeout for this HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>Defaults to 30 seconds.</remarks>
STDAPI HCHttpCallRequestGetTimeout(
    _In_opt_ HCCallHandle call,
    _Out_ uint32_t* timeoutInSeconds
    ) noexcept;

/// <summary>
/// Gets the HTTP retry delay in seconds. The default and minimum delay is 2 seconds.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls.</param>
/// <param name="retryDelayInSeconds">The retry delay in seconds.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>
/// Retries are delayed using a exponential back off.  By default, it will delay 2 seconds then the 
/// next retry will delay 4 seconds, then 8 seconds, and so on up to a max of 1 min until either
/// the call succeeds or the HTTP timeout window is reached, at which point the call will fail.
/// The delay is also jittered between the current and next delay to spread out service load.
/// The default for the HTTP timeout window is 20 seconds and can be changed using HCSettingsSetTimeoutWindow()<br />
/// <br />
/// If the service returns an HTTP error with a "Retry-After" header, then all future calls to that API 
/// will immediately fail with the original error without contacting the service until the "Retry-After" 
/// time has been reached.<br />
///<br />
/// Idempotent service calls are retried when a network error occurs or the server responds <br />
/// with one of these HTTP status codes:<br />
/// 408 (Request Timeout)<br />
/// 429 (Too Many Requests)<br />
/// 500 (Internal Server Error)<br />
/// 502 (Bad Gateway)<br />
/// 503 (Service Unavailable)<br />
/// 504 (Gateway Timeout)<br />
/// </remarks>
STDAPI HCHttpCallRequestGetRetryDelay(
    _In_opt_ HCCallHandle call,
    _In_ uint32_t* retryDelayInSeconds
    ) noexcept;

/// <summary>
/// Sets the HTTP timeout window in seconds.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to get the default for future calls.</param>
/// <param name="timeoutWindowInSeconds">The timeout window in seconds.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>
/// This controls how long to spend attempting to retry idempotent service calls before failing.<br />
/// The default is 20 seconds.<br />
///<br />
/// Idempotent service calls are retried when a network error occurs or the server responds <br />
/// with one of these HTTP status codes:<br />
/// 408 (Request Timeout)<br />
/// 429 (Too Many Requests)<br />
/// 500 (Internal Server Error)<br />
/// 502 (Bad Gateway)<br />
/// 503 (Service Unavailable)<br />
/// 504 (Gateway Timeout)<br />
/// </remarks>
STDAPI HCHttpCallRequestGetTimeoutWindow(
    _In_opt_ HCCallHandle call,
    _Out_ uint32_t* timeoutWindowInSeconds
    ) noexcept;


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse Get APIs
//

/// <summary>
/// Get the function used by the HTTP call to write the response body.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="writeFunction">The write function of this HTTP call.</param>
/// <param name="context">The context associated with this write function.</param>
/// <returns>Result code for this API operation. Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallResponseGetResponseBodyWriteFunction(
    _In_ HCCallHandle call,
    _Out_ HCHttpCallResponseBodyWriteFunction* writeFunction,
    _Out_ void** context
    ) noexcept;

/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse Set APIs
// 

/// <summary>
/// Set the response body byte buffer of the HTTP call. If a custom write callback was previously set
/// on this call handle using HCHttpCallResponseSetResponseBodyWriteFunction, this operation will fail
/// as these are mutually exclusive.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="bodyBytes">The response body bytes of the HTTP call.</param>
/// <param name="bodySize">The length in bytes of the body being set.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCHttpCallResponseSetResponseBodyBytes(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bodySize) const uint8_t* bodyBytes,
    _In_ size_t bodySize
    ) noexcept;

/// <summary>
/// Appends to the response body byte buffer of the HTTP call. If a custom write callback was previously set
/// on this call handle using HCHttpCallResponseSetResponseBodyWriteFunction, this operation will fail
/// as these are mutually exclusive.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="bodyBytes">The data to append.</param>
/// <param name="bodySize">The length in bytes of the data.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCHttpCallResponseAppendResponseBodyBytes(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bodySize) const uint8_t* bodyBytes,
    _In_ size_t bodySize
) noexcept;

/// <summary>
/// Set the HTTP status code of the HTTP call response.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallResponseSetStatusCode(
    _In_ HCCallHandle call,
    _In_ uint32_t statusCode
    ) noexcept;

/// <summary>
/// Set the network error code of the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="networkErrorCode">The network error code of the HTTP call.</param>
/// <param name="platformNetworkErrorCode">The platform specific network error code of the HTTP call to be used for logging / debugging.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallResponseSetNetworkErrorCode(
    _In_ HCCallHandle call,
    _In_ HRESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) noexcept;

/// <summary>
/// Set the platform network error message of the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="platformNetworkErrorMessage">The platform specific network error message of the HTTP call to be used for logging / debugging.</param>
/// <returns>Result code for this API operation. Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCHttpCallResponseSetPlatformNetworkErrorMessage(
    _In_ HCCallHandle call,
    _In_z_ const char* platformNetworkErrorMessage
    ) noexcept;

/// <summary>
/// Set a response header for the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="headerName">UTF-8 encoded response header name for the HTTP call.</param>
/// <param name="headerValue">UTF-8 encoded response header value for the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCHttpCallResponseSetHeader(
    _In_ HCCallHandle call,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
    ) noexcept;

/// <summary>
/// Set a response header for the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call.</param>
/// <param name="headerName">UTF-8 encoded response header name for the HTTP call.</param>
/// <param name="nameSize">The length in bytes of the header name string.</param>
/// <param name="headerValue">UTF-8 encoded response header value for the HTTP call.</param>
/// <param name="valueSize">The length in bytes of the header value string.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI HCHttpCallResponseSetHeaderWithLength(
    _In_ HCCallHandle call,
    _In_reads_(nameSize) const char* headerName,
    _In_ size_t nameSize,
    _In_reads_(valueSize) const char* headerValue,
    _In_ size_t valueSize
) noexcept;

#if !HC_NOWEBSOCKETS

/////////////////////////////////////////////////////////////////////////////////////////
// WebSocket Set APIs
// 

/// <summary>
/// Function to connects to the WebSocket.  This API returns immediately and will spin up a thread under the covers.
/// On UWP and XDK, the thread is owned and controlled by Windows::Networking::Sockets::MessageWebSocket
/// </summary>
/// <param name="uri">The UTF-8 encoded URI to connect to</param>
/// <param name="subProtocol">The UTF-8 encoded subProtocol to connect to</param>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="asyncBlock">The asyncBlock of the async task</param>
/// <param name="context">The context registered with the callback.</param>
/// <param name="env">The environment for the default callback.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
typedef HRESULT
(CALLBACK* HCWebSocketConnectFunction)(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    );

/// <summary>
/// Send message the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <param name="message">The UTF-8 encoded message to send</param>
/// <param name="asyncBlock">The asyncBlock of the async task</param>
/// <param name="context">The context registered with the callback.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
typedef HRESULT
(CALLBACK* HCWebSocketSendMessageFunction)(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
    );


/// <summary>
/// Send message the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <param name="message">The UTF-8 encoded message to send</param>
/// <param name="asyncBlock">The asyncBlock of the async task</param>
/// <param name="context">The context registered with the callback.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
typedef HRESULT
(CALLBACK* HCWebSocketSendBinaryMessageFunction)(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
    );


/// <summary>
/// Closes the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <param name="closeStatus">The close status of the WebSocket</param>
/// <param name="context">The context registered with the callback.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
typedef HRESULT
(CALLBACK* HCWebSocketDisconnectFunction)(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
    );

/// <summary>
/// Optionally allows the caller to implement the WebSocket functions.
/// </summary>
/// <param name="websocketConnectFunc">A callback that implements WebSocket connect function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform.</param>
/// <param name="websocketSendMessageFunc">A callback that implements WebSocket send message function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform.</param>
/// <param name="websocketSendBinaryMessageFunc">A callback that implements WebSocket send binary message function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform.</param>
/// <param name="websocketDisconnectFunc">A callback that implements WebSocket disconnect function as desired. 
/// Pass in nullptr to use the default implementation based on the current platform.</param>
/// <param name="context">The context pointer for the callbacks.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
STDAPI 
HCSetWebSocketFunctions(
    _In_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_ HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc,
    _In_ HCWebSocketDisconnectFunction websocketDisconnectFunc,
    _In_opt_ void* context
    ) noexcept;

/// <summary>
/// Gets the functions that implement the WebSocket functions.
/// </summary>
/// <param name="websocketConnectFunc">A callback that implements WebSocket connect function as desired.</param>
/// <param name="websocketSendMessageFunc">A callback that implements WebSocket send message function as desired. </param>
/// <param name="websocketSendBinaryMessageFunc">A callback that implements WebSocket send binary message function as desired.</param>
/// <param name="websocketDisconnectFunc">A callback that implements WebSocket disconnect function as desired.</param>
/// <param name="context">The context pointer for the callbacks.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_FAIL.</returns>
STDAPI 
HCGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketSendBinaryMessageFunction* websocketSendBinaryMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc,
    _Out_ void** context
    ) noexcept;

/// <summary>
/// Get the proxy URI for the WebSocket.
/// </summary>
/// <param name="websocket">The handle of the WebSocket.</param>
/// <param name="proxyUri">The UTF-8 encoded proxy URI for the WebSocket.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI 
HCWebSocketGetProxyUri(
    _In_ HCWebsocketHandle websocket,
    _Out_ const char** proxyUri
    ) noexcept;

/// <summary>
/// Get a header for the WebSocket.
/// </summary>
/// <param name="websocket">The handle of the WebSocket.</param>
/// <param name="headerName">UTF-8 encoded header name for the WebSocket.</param>
/// <param name="headerValue">UTF-8 encoded header value for the WebSocket.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, E_OUTOFMEMORY, or E_FAIL.</returns>
STDAPI 
HCWebSocketGetHeader(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
    ) noexcept;

/// <summary>
/// Gets the number of headers in the WebSocket.
/// </summary>
/// <param name="websocket">The handle of the WebSocket.</param>
/// <param name="numHeaders">the number of headers in the WebSocket.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI 
HCWebSocketGetNumHeaders(
    _In_ HCWebsocketHandle websocket,
    _Out_ uint32_t* numHeaders
    ) noexcept;

/// <summary>
/// Gets the headers at specific zero based index in the WebSocket.
/// </summary>
/// <param name="websocket">The handle of the WebSocket.</param>
/// <param name="headerIndex">Specific zero based index of the header.</param>
/// <param name="headerName">UTF-8 encoded header name for the HTTP call.</param>
/// <param name="headerValue">UTF-8 encoded header value for the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
/// <remarks>Use HCHttpCallGetNumHeaders() to know how many headers there are in the HTTP call.</remarks>
STDAPI 
HCWebSocketGetHeaderAtIndex(
    _In_ HCWebsocketHandle websocket,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
) noexcept;

#endif // !HC_NOWEBSOCKETS

}
