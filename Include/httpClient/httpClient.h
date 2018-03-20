// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include <httpClient/types.h>
//#include <httpClient/task.h>
#include <httpClient/mock.h>
#include <httpClient/trace.h>
#include <httpClient/async.h>
#include <httpClient/asyncQueue.h>

#if defined(__cplusplus)
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
// Memory APIs
//

/// <summary>
/// A callback invoked every time a new memory buffer must be dynamically allocated by the library.
/// This callback is optionally installed by calling HCMemSetFunctions()
/// 
/// The callback must allocate and return a pointer to a contiguous block of memory of the 
/// specified size that will remain valid until the app's corresponding HC_MEM_FREE_FUNC 
/// callback is invoked to release it.
/// 
/// Every non-null pointer returned by this method will be subsequently passed to the corresponding
/// HC_MEM_FREE_FUNC callback once the memory is no longer needed.
/// </summary>
/// <returns>A pointer to an allocated block of memory of the specified size, or a null 
/// pointer if allocation failed.</returns>
/// <param name="size">The size of the allocation to be made. This value will never be zero.</param>
/// <param name="memoryTypeId">An opaque identifier representing the internal category of 
/// memory being allocated.</param>
typedef _Ret_maybenull_ _Post_writable_byte_size_(size) void*
(HC_CALLING_CONV* HC_MEM_ALLOC_FUNC)(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    );

/// <summary>
/// A callback invoked every time a previously allocated memory buffer is no longer needed by 
/// the library and can be freed. This callback is optionally installed by calling HCMemSetFunctions()
///
/// The callback is invoked whenever the library has finished using a memory buffer previously 
/// returned by the app's corresponding HC_MEM_ALLOC_FUNC such that the application can free the
/// memory buffer.
/// </summary>
/// <param name="pointer">The pointer to the memory buffer previously allocated. This value will
/// never be a null pointer.</param>
/// <param name="memoryTypeId">An opaque identifier representing the internal category of 
/// memory being allocated.</param>
typedef void
(HC_CALLING_CONV* HC_MEM_FREE_FUNC)(
    _In_ _Post_invalid_ void* pointer,
    _In_ HC_MEMORY_TYPE memoryType
    );

/// <summary>
/// Optionally sets the memory hook functions to allow callers to control route memory 
/// allocations to their own memory manager. This must be called before HCGlobalInitialize() 
/// and can not be called again until HCGlobalCleanup()
///
/// This method allows the application to install custom memory allocation routines in order 
/// to service all requests for new memory buffers instead of using default allocation routines.
///
/// The <paramref name="memAllocFunc" /> and <paramref name="memFreeFunc" /> parameters can be null
/// pointers to restore the default routines. Both callback pointers must be null or both must 
/// be non-null. Mixing custom and default routines is not permitted.
/// </summary>
/// <param name="memAllocFunc">A pointer to the custom allocation callback to use, or a null 
/// pointer to restore the default.</param>
/// <param name="memFreeFunc">A pointer to the custom freeing callback to use, or a null 
/// pointer to restore the default.</param>
HC_API HC_RESULT HC_CALLING_CONV
HCMemSetFunctions(
    _In_opt_ HC_MEM_ALLOC_FUNC memAllocFunc,
    _In_opt_ HC_MEM_FREE_FUNC memFreeFunc
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the memory hook functions to allow callers to control route memory allocations to their 
/// own memory manager.  This method allows the application get the default memory allocation routines.
/// This can be used along with HCMemSetFunctions() to monitor all memory allocations.
/// </summary>
/// <param name="memAllocFunc">Set to the current allocation callback.  Returns the default routine 
/// if not previously set</param>
/// <param name="memFreeFunc">Set to the to the current memory free callback.  Returns the default 
/// routine if not previously set</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCMemGetFunctions(
    _Out_ HC_MEM_ALLOC_FUNC* memAllocFunc,
    _Out_ HC_MEM_FREE_FUNC* memFreeFunc
    ) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// Global APIs
// 

/// <summary>
/// Initializes the library instance.
/// This must be called before any other method, except for HCMemSetFunctions() and HCMemGetFunctions()
/// Should have a corresponding call to HCGlobalCleanup().
/// </summary>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCGlobalInitialize() HC_NOEXCEPT;

/// <summary>
/// Immediately reclaims all resources associated with the library.
/// If you called HCMemSetFunctions(), call this before shutting down your app's memory manager.
/// </summary>
HC_API void HC_CALLING_CONV
HCGlobalCleanup() HC_NOEXCEPT;

/// <summary>
/// Returns the version of the library
/// </summary>
/// <param name="version">The version of the library in the format of release_year.release_month.date.rev.  
/// For example, 2017.07.20170710.01</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR* version) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// Logging APIs
//

/// <summary>
/// Diagnostic level used by logging
/// </summary>
typedef enum HC_LOG_LEVEL
{
    /// <summary>
    /// No logging
    /// </summary>
    LOG_OFF,

    /// <summary>
    /// Log only errors
    /// </summary>
    LOG_ERROR,

    /// <summary>
    /// Log warnings and errors
    /// </summary>
    LOG_WARNING,

    /// <summary>
    /// Log important, warnings and errors
    /// </summary>
    LOG_IMPORTANT,

    /// <summary>
    /// Log info, important, warnings and errors
    /// </summary>
    LOG_INFORMATION,

    /// <summary>
    /// Log everything
    /// </summary>
    LOG_VERBOSE
} HC_LOG_LEVEL;

/// <summary>
/// Sets the log level for the library.  Logs are sent the debug output
/// </summary>
/// <param name="logLevel">Log level</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCSettingsSetLogLevel(
    _In_ HC_LOG_LEVEL logLevel
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the log level for the library
/// </summary>
/// <param name="logLevel">Log level</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCSettingsGetLogLevel(
    _Out_ HC_LOG_LEVEL* logLevel
    ) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// Http APIs
//

/// <summary>
/// Creates an HTTP call handle
///
/// First create a HTTP handle using HCHttpCallCreate()
/// Then call HCHttpCallRequestSet*() to prepare the HC_CALL_HANDLE
/// Then call HCHttpCallPerform() to perform HTTP call using the HC_CALL_HANDLE.
/// This call is asynchronous, so the work will be done on a background thread and will return via the callback.
/// This task executes immediately so no need to call HCTaskProcessNextPendingTask().
/// Call HCTaskProcessNextCompletedTask(taskGroupId) on the thread where you want the 
/// callback execute, using the same taskGroupId as passed to HCHttpCallPerform().
/// 
/// Inside the callback or after the task is done using HCTaskIsCompleted() or 
/// HCTaskWaitForCompleted(), then get the result of the HTTP call by calling 
/// HCHttpCallResponseGet*() to get the HTTP response of the HC_CALL_HANDLE.
/// 
/// When the HC_CALL_HANDLE is no longer needed, call HCHttpCallCloseHandle() to free the 
/// memory associated with the HC_CALL_HANDLE
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallCreate(
    _Out_ HC_CALL_HANDLE* call
    ) HC_NOEXCEPT;

/// <summary>
/// Callback definition for the HTTP completion routine used by HCHttpCallPerform()
/// </summary>
/// <param name="completionRoutineContext">The context passed to the completion routine</param>
/// <param name="call">The handle of the HTTP call</param>
typedef void(* HCHttpCallPerformCompletionRoutine)(
    _In_opt_ void* completionRoutineContext,
    _In_ HC_CALL_HANDLE call
    );

/// <summary>
/// Perform HTTP call using the HC_CALL_HANDLE
///
/// First create a HTTP handle using HCHttpCallCreate()
/// Then call HCHttpCallRequestSet*() to prepare the HC_CALL_HANDLE
/// Then call HCHttpCallPerform() to perform HTTP call using the HC_CALL_HANDLE.
/// This call is asynchronous, so the work will be done on a background thread and will return via the callback.
/// This task executes immediately so no need to call HCTaskProcessNextPendingTask().
/// Call HCTaskProcessNextCompletedTask(taskGroupId) on the thread where you want the 
/// callback execute, using the same taskGroupId as passed to HCHttpCallPerform().
/// 
/// Inside the callback or after the task is done using HCTaskIsCompleted() or 
/// HCTaskWaitForCompleted(), then get the result of the HTTP call by calling 
/// HCHttpCallResponseGet*() to get the HTTP response of the HC_CALL_HANDLE.
/// 
/// When the HC_CALL_HANDLE is no longer needed, call HCHttpCallCloseHandle() to free the 
/// memory associated with the HC_CALL_HANDLE
///
/// HCHttpCallPerform can only be called once.  Create new HC_CALL_HANDLE to repeat the call.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="taskHandle">The task handle returned by the operation. If the API fails, HC_TASK_HANDLE will be 0</param>
/// <param name="taskSubsystemId">
/// The task subsystem ID to assign to this task.  This is the ID of the caller's subsystem.
/// If this isn't needed or unknown, just pass in 0.
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// can each expose thier own version of ProcessNextPendingTask() and 
/// ProcessNextCompletedTask() APIs that operate independently.
/// </param>
/// <param name="taskGroupId">
/// The task group ID to assign to this task.  The ID is defined by the caller and can be any number.
/// HCTaskProcessNextCompletedTask(taskSubsystemId, taskGroupId) will only process completed tasks that have a
/// matching taskSubsystemId and taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads and a set of subsystems.
/// If the group ID isn't needed, just pass in 0.
/// </param>
/// <param name="completionRoutineContext">The context to pass in to the completionRoutine callback</param>
/// <param name="completionRoutine">A callback that's called when the HTTP call completes</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ AsyncBlock* async
    ) HC_NOEXCEPT;

/// <summary>
/// Increments the reference count on the call object.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <returns>Returns the duplicated handle.</returns>
HC_CALL_HANDLE HCHttpCallDuplicateHandle(
    _In_ HC_CALL_HANDLE call
    ) HC_NOEXCEPT;

/// <summary>
/// Decrements the reference count on the call object. 
/// When the HC_CALL_HANDLE ref count is 0, HCHttpCallCloseHandle() will 
/// free the memory associated with the HC_CALL_HANDLE
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallCloseHandle(
    _In_ HC_CALL_HANDLE call
    ) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallRequest Set APIs
//

/// <summary>
/// Sets the url and method for the HTTP call
/// This must be called prior to calling HCHttpCallPerform.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="method">Method for the HTTP call</param>
/// <param name="url">URL for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR method,
    _In_z_ PCSTR url
    ) HC_NOEXCEPT;

/// <summary>
/// Set the request body bytes of the HTTP call
/// This must be called prior to calling HCHttpCallPerform.
/// </summary> 
/// <param name="call">The handle of the HTTP call</param>
/// <param name="requestBodyBytes">The request body bytes of the HTTP call.</param>
/// <param name="requestBodySize">The length in bytes of the body being set.</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyBytes(
    _In_ HC_CALL_HANDLE call,
    _In_reads_bytes_(requestBodySize) const BYTE* requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) HC_NOEXCEPT;

/// <summary>
/// Set the request body string of the HTTP call
/// This must be called prior to calling HCHttpCallPerform.
/// </summary> 
/// <param name="call">The handle of the HTTP call</param>
/// <param name="requestBodyString">The request body string of the HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR requestBodyString
) HC_NOEXCEPT;

/// <summary>
/// Set a request header for the HTTP call
/// This must be called prior to calling HCHttpCallPerform.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">request header name for the HTTP call</param>
/// <param name="headerValue">request header value for the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR headerName,
    _In_z_ PCSTR headerValue
    ) HC_NOEXCEPT;

/// <summary>
/// Sets if retry is allowed for this HTTP call
/// Defaults to true 
/// This must be called prior to calling HCHttpCallPerform.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="retryAllowed">If retry is allowed for this HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ bool retryAllowed
    ) HC_NOEXCEPT;

/// <summary>
/// Sets the timeout for this HTTP call.
/// Defaults to 30 seconds
/// This must be called prior to calling HCHttpCallPerform.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="timeoutInSeconds">The timeout for this HTTP call.</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    ) HC_NOEXCEPT;

/// <summary>
/// Sets the HTTP retry delay in seconds. The default and minimum delay is 2 seconds.
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
/// Idempotent service calls are retried when a network error occurs or the server responds with 
/// one of these HTTP status codes:
/// 408 (Request Timeout)
/// 429 (Too Many Requests)
/// 500 (Internal Server Error)
/// 502 (Bad Gateway)
/// 503 (Service Unavailable)
/// 504 (Gateway Timeout)
///
/// This must be called prior to calling HCHttpCallPerform.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="retryDelayInSeconds">The retry delay in seconds</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t retryDelayInSeconds
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
///
/// This must be called prior to calling HCHttpCallPerform.
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="timeoutWindowInSeconds">The timeout window in seconds</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutWindowInSeconds
    ) HC_NOEXCEPT;


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse Get APIs
// 

/// <summary>
/// Get the response body string of the HTTP call
/// This can only be called after calling HCHttpCallPerform when the HTTP task is completed.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="responseString">
/// The response body string of the HTTP call
/// The memory for the returned string pointer remains valid for the life of the HC_CALL_HANDLE object until HCHttpCallCloseHandle() is called on it.
/// </param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetResponseString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR* responseString
    ) HC_NOEXCEPT;

/// <summary>
/// Get the HTTP status code of the HTTP call response
/// This can only be called after calling HCHttpCallPerform when the HTTP task is completed.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response</param>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* statusCode
    );

/// <summary>
/// Get the network error code of the HTTP call
/// This can only be called after calling HCHttpCallPerform when the HTTP task is completed.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="networkErrorCode">The network error code of the HTTP call. Possible values are HC_OK, or HC_E_FAIL.</param>
/// <param name="platformNetworkErrorCode">The platform specific network error code of the HTTP call to be used for logging / debugging</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetNetworkErrorCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ HC_RESULT* networkErrorCode,
    _Out_ uint32_t* platformNetworkErrorCode
    ) HC_NOEXCEPT;

/// <summary>
/// Get a response header for the HTTP call for a given header name
/// This can only be called after calling HCHttpCallPerform when the HTTP task is completed.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">Response header name for the HTTP call
/// The memory for the returned string pointer remains valid for the life of the HC_CALL_HANDLE object until HCHttpCallCloseHandle() is called on it.
/// </param>
/// <param name="headerValue">Response header value for the HTTP call.
/// Returns nullptr if the header doesn't exist.
/// The memory for the returned string pointer remains valid for the life of the HC_CALL_HANDLE object until HCHttpCallCloseHandle() is called on it.
/// </param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the number of response headers in the HTTP call
/// This can only be called after calling HCHttpCallPerform when the HTTP task is completed.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="numHeaders">The number of response headers in the HTTP call</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the response headers at specific zero based index in the HTTP call.
/// Use HCHttpCallResponseGetNumHeaders() to know how many response headers there are in the HTTP call.
/// This can only be called after calling HCHttpCallPerform when the HTTP task is completed.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerIndex">Specific zero based index of the response header</param>
/// <param name="headerName">Response header name for the HTTP call.
/// The memory for the returned string pointer remains valid for the life of the HC_CALL_HANDLE object until HCHttpCallCloseHandle() is called on it.
/// </param>
/// <param name="headerValue">Response header value for the HTTP call.
/// The memory for the returned string pointer remains valid for the life of the HC_CALL_HANDLE object until HCHttpCallCloseHandle() is called on it.
/// </param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    ) HC_NOEXCEPT;

/////////////////////////////////////////////////////////////////////////////////////////
// WebSocket APIs
// 

/// <summary>
/// Creates an WebSocket handle
///
/// WebSocket usage:
/// Setup the handler functions with HCWebSocketSetFunctions()
/// Create a WebSocket handle using HCWebSocketCreate()
/// Call HCWebSocketSetProxyUri() and HCWebSocketSetHeader() to prepare the HC_WEBSOCKET_HANDLE
/// Call HCWebSocketConnect() to connect the WebSocket using the HC_WEBSOCKET_HANDLE.
/// Call HCWebSocketSendMessage() to send a message to the WebSocket using the HC_WEBSOCKET_HANDLE.
/// Call HCWebSocketDisconnect() to disconnect the WebSocket using the HC_WEBSOCKET_HANDLE.
/// Call HCWebSocketCloseHandle() when done with the HC_WEBSOCKET_HANDLE to free the associated memory
/// </summary>
/// <param name="websocket">The handle of the websocket</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketCreate(
    _Out_ HC_WEBSOCKET_HANDLE* websocket
    ) HC_NOEXCEPT;

/// <summary>
/// Set the proxy URI for the WebSocket
/// This must be called prior to calling HCWebsocketConnect.
/// </summary>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="proxyUri">The proxy URI for the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSetProxyUri(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR proxyUri
    ) HC_NOEXCEPT;

/// <summary>
/// Set a header for the WebSocket
/// This must be called prior to calling HCWebsocketConnect.
/// </summary>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="headerName">Header name for the WebSocket</param>
/// <param name="headerValue">Header value for the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSetHeader(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR headerName,
    _In_z_ PCSTR headerValue
    ) HC_NOEXCEPT;


/// <summary>
/// A callback invoked every time a WebSocket receives an incoming message
/// </summary>
/// <param name="websocket">Handle to the WebSocket that this message was sent to</param>
/// <param name="incomingBodyString">Body of the incoming message as a string value, only if the message type is UTF-8.</param>
typedef void
(HC_CALLING_CONV* HC_WEBSOCKET_MESSAGE_FUNC)(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR incomingBodyString
    );

/// <summary>
/// A callback invoked when a WebSocket is closed
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <param name="closeStatus">The status of why the WebSocket was closed</param>
typedef void
(HC_CALLING_CONV* HC_WEBSOCKET_CLOSE_EVENT_FUNC)(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CLOSE_STATUS closeStatus
    );

/// <summary>
/// Sets the WebSocket functions to allow callers to respond to incoming messages and WebSocket close events.
/// </summary>
/// <param name="messageFunc">A pointer to the message handling callback to use, or a null pointer to remove.</param>
/// <param name="closeFunc">A pointer to the close callback to use, or a null pointer to remove.</param>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSetFunctions(
    _In_opt_ HC_WEBSOCKET_MESSAGE_FUNC messageFunc,
    _In_opt_ HC_WEBSOCKET_CLOSE_EVENT_FUNC closeFunc
    ) HC_NOEXCEPT;


/// <summary>
/// Used by HCWebSocketConnect() and HCWebSocketSendMessage()
/// </summary>
struct WebSocketCompletionResult
{
    /// <param name="websocket">The handle of the HTTP call</param>
    HC_WEBSOCKET_HANDLE websocket;

    /// <param name="errorCode">The error code of the call. Possible values are HC_OK, or HC_E_FAIL.</param>
    HC_RESULT errorCode;

    /// <param name="platformErrorCode">The platform specific network error code of the call to be used for logging / debugging</param>
    uint32_t platformErrorCode;
};

/// <summary>
/// Connects to the WebSocket.
/// On UWP and XDK, the connection thread is owned and controlled by Windows::Networking::Sockets::MessageWebSocket
/// </summary>
/// <param name="uri">The URI to connect to</param>
/// <param name="websocket">The handle of the WebSocket</param>
/// <param name="args">Struct for describing the WebSocket connection args</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, HC_E_OUTOFMEMORY, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ AsyncBlock* async
    ) HC_NOEXCEPT;

HC_API HC_RESULT HC_CALLING_CONV
HCGetWebSocketConnectResult(
    _In_ AsyncBlock* async,
    _In_ WebSocketCompletionResult* result
    ) HC_NOEXCEPT;

/// <summary>
/// Send message the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message,
    _In_ AsyncBlock* async
    ) HC_NOEXCEPT;

HC_API HC_RESULT HC_CALLING_CONV
HCGetWebSocketSendMessageResult(
    _In_ AsyncBlock* async,
    _In_ WebSocketCompletionResult* result
    ) HC_NOEXCEPT;

/// <summary>
/// Disconnects / closes the WebSocket
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketDisconnect(
    _In_ HC_WEBSOCKET_HANDLE websocket
    ) HC_NOEXCEPT;

/// <summary>
/// Increments the reference count on the call object.
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <returns>Returns the duplicated handle.</returns>
HC_WEBSOCKET_HANDLE HCWebSocketDuplicateHandle(
    _In_ HC_WEBSOCKET_HANDLE websocket
    ) HC_NOEXCEPT;

/// <summary>
/// Decrements the reference count on the WebSocket object. 
/// When the ref count is 0, HCWebSocketCloseHandle() will 
/// free the memory associated with the HC_WEBSOCKET_HANDLE
/// </summary>
/// <param name="websocket">Handle to the WebSocket</param>
/// <returns>Result code for this API operation.  Possible values are HC_OK, HC_E_INVALIDARG, or HC_E_FAIL.</returns>
HC_API HC_RESULT HC_CALLING_CONV
HCWebSocketCloseHandle(
    _In_ HC_WEBSOCKET_HANDLE websocket
    ) HC_NOEXCEPT;


#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

