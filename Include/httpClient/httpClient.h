// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "types.h"
#include "task.h"
#include "httpProvider.h"
#include "mock.h"

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
HC_API void HC_CALLING_CONV
HCMemSetFunctions(
    _In_opt_ HC_MEM_ALLOC_FUNC memAllocFunc,
    _In_opt_ HC_MEM_FREE_FUNC memFreeFunc
    );

/// <summary>
/// Gets the memory hook functions to allow callers to control route memory allocations to their 
/// own memory manager.  This method allows the application get the default memory allocation routines.
/// This can be used along with HCMemSetFunctions() to monitor all memory allocations.
/// </summary>
/// <param name="memAllocFunc">Set to the current allocation callback.  Returns the default routine 
/// if not previously set</param>
/// <param name="memFreeFunc">Set to the to the current memory free callback.  Returns the default 
/// routine if not previously set</param>
HC_API void HC_CALLING_CONV
HCMemGetFunctions(
    _Out_ HC_MEM_ALLOC_FUNC* memAllocFunc,
    _Out_ HC_MEM_FREE_FUNC* memFreeFunc
    );


/////////////////////////////////////////////////////////////////////////////////////////
// Global APIs
// 

/// <summary>
/// Initializes the library instance.
/// This must be called before any other method, except for HCMemSetFunctions() and HCMemGetFunctions()
/// Should have a corresponding call to HCGlobalCleanup().
/// </summary>
HC_API void HC_CALLING_CONV
HCGlobalInitialize();

/// <summary>
/// Immediately reclaims all resources associated with the library.
/// If you called HCMemSetFunctions(), call this before shutting down your app's memory manager.
/// </summary>
HC_API void HC_CALLING_CONV
HCGlobalCleanup();

/// <summary>
/// Returns the version of the library
/// </summary>
/// <param name="version">The version of the library in the format of release_year.release_month.date.rev.  
/// For example, 2017.07.20170710.01</param>
HC_API void HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR* version);


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
    /// Log everything
    /// </summary>
    LOG_VERBOSE
} HC_LOG_LEVEL;

/// <summary>
/// Sets the log level for the library.  Logs are sent the debug output
/// </summary>
/// <param name="logLevel">Log level</param>
HC_API void HC_CALLING_CONV
HCSettingsSetLogLevel(
    _In_ HC_LOG_LEVEL logLevel
    );

/// <summary>
/// Gets the log level for the library
/// </summary>
/// <param name="logLevel">Log level</param>
HC_API void HC_CALLING_CONV
HCSettingsGetLogLevel(
    _Out_ HC_LOG_LEVEL* logLevel
    );


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
/// When the HC_CALL_HANDLE is no longer needed, call HCHttpCallCleanup() to free the 
/// memory associated with the HC_CALL_HANDLE
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallCreate(
    _Out_ HC_CALL_HANDLE* call
    );

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
/// When the HC_CALL_HANDLE is no longer needed, call HCHttpCallCleanup() to free the 
/// memory associated with the HC_CALL_HANDLE
/// </summary>
/// <param name="taskGroupId">
/// The task group ID to assign to this task.  The ID is defined by the caller and can be any number.
/// HCTaskProcessNextCompletedTask(taskGroupId) will only process completed tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </param>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="completionRoutineContext">The context to pass in to the completionRoutine callback</param>
/// <param name="completionRoutine">A callback that's called when the HTTP call completes</param>
HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCHttpCallPerform(
    _In_ uint64_t taskGroupId,
    _In_ HC_CALL_HANDLE call,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCHttpCallPerformCompletionRoutine completionRoutine
    );

/// <summary>
/// When the HC_CALL_HANDLE is no longer needed, call HCHttpCallCleanup() to free the 
/// memory associated with the HC_CALL_HANDLE
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallCleanup(
    _In_ HC_CALL_HANDLE call
    );


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallRequest Set APIs
//

/// <summary>
/// Sets the url and method for the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="method">Method for the HTTP call</param>
/// <param name="url">URL for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR method,
    _In_ PCSTR url
    );

/// <summary>
/// Set the request body string of the HTTP call
/// </summary> 
/// <param name="call">The handle of the HTTP call</param>
/// <param name="requestBodyString">the request body string of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR requestBodyString
    );

/// <summary>
/// Set a request header for the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">request header name for the HTTP call</param>
/// <param name="headerValue">request header value for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _In_ PCSTR headerValue
    );

/// <summary>
/// Sets if retry is allowed for this HTTP call
/// Defaults to true 
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="retryAllowed">If retry is allowed for this HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ bool retryAllowed
    );

/// <summary>
/// Sets the timeout for this HTTP call.
/// Defaults to 30 seconds
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="timeoutInSeconds">The timeout for this HTTP call.</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    );

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
/// </summary>
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="retryDelayInSeconds">The retry delay in seconds</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t retryDelayInSeconds
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
/// <param name="call">The handle of the HTTP call.  Pass nullptr to set the default for future calls</param>
/// <param name="timeoutWindowInSeconds">The timeout window in seconds</param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutWindowInSeconds
    );

/// <summary>
/// Sets if assert are enabled if throttled.
/// TODO: The asserts will not fire in RETAIL sandbox, and this setting has has no affect in RETAIL sandboxes.
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
HCHttpCallRequestSetAssertsForThrottling(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ bool enableAssertsForThrottling
    );


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse Get APIs
// 

/// <summary>
/// Get the response body string of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="responseString">the response body string of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetResponseString(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ PCSTR* responseString
    );

/// <summary>
/// Get the HTTP status code of the HTTP call response
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="statusCode">the HTTP status code of the HTTP call response</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetStatusCode(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* statusCode
    );

/// <summary>
/// Get the network error code of the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="errorCode">the network error code of the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetErrorCode(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* errorCode
    );

/// <summary>
/// Get a response header for the HTTP call for a given header name
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerName">Response header name for the HTTP call</param>
/// <param name="headerValue">Response header value for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    );

/// <summary>
/// Gets the number of response headers in the the HTTP call
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="numHeaders">The number of response headers in the the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    );

/// <summary>
/// Gets the response headers at specific zero based index in the the HTTP call.
/// Use HCHttpCallResponseGetNumHeaders() to know how many response headers there are in the HTTP call.
/// </summary>
/// <param name="call">The handle of the HTTP call</param>
/// <param name="headerIndex">Specific zero based index of the response header</param>
/// <param name="headerName">Response header name for the HTTP call</param>
/// <param name="headerValue">Response header value for the HTTP call</param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    );

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

