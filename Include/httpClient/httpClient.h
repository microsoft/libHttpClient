// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif


//
// HCMem APIs
//

/// <summary>
/// A callback invoked every time a new memory buffer must be dynamically allocated by the library.
/// This callback is optionally installed by calling HCMemSetFunctions()
/// 
/// The callback must allocate and return a pointer to a contiguous block of memory of the specified size that will
/// remain valid until the app's corresponding HC_MEM_FREE_FUNC callback is invoked to release it.
/// 
/// Every non-null pointer returned by this method will be subsequently passed to the corresponding
/// HC_MEM_FREE_FUNC callback once the memory is no longer needed.
/// </summary>
/// <returns>A pointer to an allocated block of memory of the specified size, or a null pointer if allocation failed.</returns>
/// <param name="size">The size of the allocation to be made. This value will never be zero.</param>
/// <param name="memoryTypeId">An opaque identifier representing the internal category of memory being allocated.</param>
typedef _Ret_maybenull_ _Post_writable_byte_size_(size) void*
(HC_CALLING_CONV* HC_MEM_ALLOC_FUNC)(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    );

/// <summary>
/// A callback invoked every time a previously allocated memory buffer is no longer needed by the library and can be freed.
/// This callback is optionally installed by calling HCMemSetFunctions()
///
/// The callback is invoked whenever the library has finished using a memory buffer previously returned by the
/// app's corresponding HC_MEM_ALLOC_FUNC such that the application can free the
/// memory buffer.
/// </summary>
/// <param name="pointer">The pointer to the memory buffer previously allocated. This value will never be a null pointer.</param>
/// <param name="memoryTypeId">An opaque identifier representing the internal category of memory being allocated.</param>
typedef void
(HC_CALLING_CONV* HC_MEM_FREE_FUNC)(
    _In_ _Post_invalid_ void* pointer,
    _In_ HC_MEMORY_TYPE memoryType
    );

/// <summary>
/// Optionally sets the memory hook functions to allow callers to control route memory allocations to thier own memory manager
/// This must be called before HCGlobalInitialize() and can not be called again until HCGlobalCleanup()
///
/// This method allows the application to install custom memory allocation routines in order to service all requests
/// for new memory buffers instead of using default allocation routines.
///
/// The <paramref name="memAllocFunc" /> and <paramref name="memFreeFunc" /> parameters can be null
/// pointers to restore the default routines. Both callback pointers must be null or both must be non-null. Mixing
/// custom and default routines is not permitted.
/// </summary>
/// <param name="memAllocFunc">A pointer to the custom allocation callback to use, or a null pointer to restore the default.</param>
/// <param name="memFreeFunc">A pointer to the custom freeing callback to use, or a null pointer to restore the default.</param>
HC_API void HC_CALLING_CONV
HCMemSetFunctions(
    _In_opt_ HC_MEM_ALLOC_FUNC memAllocFunc,
    _In_opt_ HC_MEM_FREE_FUNC memFreeFunc
    );

/// <summary>
/// Gets the memory hook functions to allow callers to control route memory allocations to their own memory manager
/// This method allows the application get the default memory allocation routines.
/// This can be used along with HCMemSetFunctions() to monitor all memory allocations.
/// </summary>
/// <param name="memAllocFunc">Set to the current allocation callback.  Returns the default routine if not previously set</param>
/// <param name="memFreeFunc">Set to the to the current memory free callback.  Returns the default routine if not previously set</param>
HC_API void HC_CALLING_CONV
HCMemGetFunctions(
    _Out_ HC_MEM_ALLOC_FUNC* memAllocFunc,
    _Out_ HC_MEM_FREE_FUNC* memFreeFunc
    );


// 
// HCGlobal APIs
// 

/// <summary>
/// </summary>
/// <param name="completionRoutineContext"></param>
/// <param name="call"></param>
typedef void(*HCHttpCallPerformCompletionRoutine)(
    _In_ void* completionRoutineContext,
    _In_ HC_CALL_HANDLE call
    );

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
/// <returns>The version of the library in the format of release_year.release_month.date.rev.  For example, 2017.07.20170710.01</returns>
HC_API void HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR_T* version);

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="taskHandle"></param>
typedef void
(HC_CALLING_CONV* HC_HTTP_CALL_PERFORM_FUNC)(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_ASYNC_TASK_HANDLE taskHandle
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
/// Returns the current HC_HTTP_CALL_PERFORM_FUNC callback which implements the HTTP perform function on the current platform.
/// This can be used along with HCGlobalSetHttpCallPerformFunction() to monitor all HTTP calls.
/// </summary>
/// <param name="performFunc">Set to the current HTTP perform function. Returns the default routine if not previously set</param>
HC_API void HC_CALLING_CONV
HCGlobalGetHttpCallPerformFunction(
    _Out_ HC_HTTP_CALL_PERFORM_FUNC* performFunc
    );


// 
// HCThead APIs
// 

/// <summary>
/// </summary>
/// <param name="executionRoutineContext"></param>
/// <param name="taskHandle"></param>
typedef void
(HC_CALLING_CONV* HC_ASYNC_OP_FUNC)(
    _In_opt_ void* executionRoutineContext,
    _In_ HC_ASYNC_TASK_HANDLE taskHandle
    );

/// <summary>
/// </summary>
/// <param name="executionRoutine"></param>
/// <param name="executionRoutineContext"></param>
/// <param name="writeResultsRoutine"></param>
/// <param name="writeResultsRoutineContext"></param>
/// <param name="completionRoutine"></param>
/// <param name="completionRoutineContext"></param>
/// <param name="executeNow"></param>
HC_API void HC_CALLING_CONV
HCThreadQueueAsyncOp(
    _In_ HC_ASYNC_OP_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_ HC_ASYNC_OP_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _In_ bool executeNow
    );

/// <summary>
/// </summary>
/// <param name="taskHandle"></param>
HC_API void HC_CALLING_CONV
HCThreadSetResultsReady(
    _In_ HC_ASYNC_TASK_HANDLE taskHandle
    );

/// <summary>
/// </summary>
HC_API bool HC_CALLING_CONV
HCThreadIsAsyncOpPending();

#if UWP_API
	/// <summary>
	/// </summary>
	HC_API HANDLE HC_CALLING_CONV
	HCThreadGetAsyncOpPendingHandle();

	/// <summary>
	/// </summary>
	HC_API HANDLE HC_CALLING_CONV
	HCThreadGetAsyncOpCompletedHandle();
#endif

/// <summary>
/// </summary>
HC_API void HC_CALLING_CONV
HCThreadProcessPendingAsyncOp();

/// <summary>
/// </summary>
HC_API void HC_CALLING_CONV
HCThreadProcessCompletedAsyncOp();

/// <summary>
/// Set to 0 to disable
/// Defaults to 2
/// </summary>
/// <param name="targetNumThreads"></param>
HC_API void HC_CALLING_CONV
HCThreadSetNumThreads(_In_ uint32_t targetNumThreads);

/// <summary>
/// Optionally configures the processor on which internal threads will run.
///
/// For exclusive resource applications, the threads are guaranteed to run on the specified processor. For universal
/// Windows applications, the specified processor is used as the thread's ideal processor and is only a hint for the
/// scheduler.
///
/// This method may be called at any time before or after chat_manager::initialize() and will take effect
/// immediately. 
/// </summary>
/// <param name="threadIndex">Zero based index of the thread.  Pass -1 to apply for all threads</param>
/// <param name="processorNumber">The zero-based processor number on which the internal threads should run. 
/// Pass 0xFFFFFFFF to specify any processors</param>
HC_API void HC_CALLING_CONV
HCThreadSetProcessor(_In_ int threadIndex, _In_ uint32_t processorNumber);


//
// HCSettings APIs
//

/// <summary>
/// </summary>
typedef enum HC_DIAGNOSTICS_TRACE_LEVEL
{
    TRACE_OFF,
    TRACE_ERROR,
    TRACE_VERBOSE
} HC_DIAGNOSTICS_TRACE_LEVEL;

/// <summary>
/// </summary>
/// <param name="traceLevel"></param>
HC_API void HC_CALLING_CONV
HCSettingsSetDiagnosticsTraceLevel(
    _In_ HC_DIAGNOSTICS_TRACE_LEVEL traceLevel
    );

/// <summary>
/// </summary>
/// <param name="traceLevel"></param>
HC_API void HC_CALLING_CONV
HCSettingsGetDiagnosticsTraceLevel(
    _Out_ HC_DIAGNOSTICS_TRACE_LEVEL* traceLevel
    );

/// <summary>
/// </summary>
/// <param name="timeoutWindowInSeconds"></param>
HC_API void HC_CALLING_CONV
HCSettingsSetTimeoutWindow(
    _In_ uint32_t timeoutWindowInSeconds
    );

/// <summary>
/// </summary>
/// <param name="timeoutWindowInSeconds"></param>
HC_API void HC_CALLING_CONV
HCSettingsGetTimeoutWindow(
    _Out_ uint32_t* timeoutWindowInSeconds
    );

/// <summary>
/// </summary>
/// <param name="enableAssertsForThrottling"></param>
HC_API void HC_CALLING_CONV
HCSettingsSetAssertsForThrottling(
    _In_ bool enableAssertsForThrottling
    );

/// <summary>
/// </summary>
/// <param name="enableAssertsForThrottling"></param>
HC_API void HC_CALLING_CONV
HCSettingsGetAssertsForThrottling(
    _Out_ bool* enableAssertsForThrottling
    );

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
/// calling HCSettingsAddMockCall(). You do not need to call HCHttpCallCleanup() for 
/// the HC_CALL_HANDLEs passed to HCSettingsAddMockCall().
/// 
/// You can set multiple active mock responses by calling HCSettingsAddMockCall() multiple 
/// times with a set of mock responses. If the HTTP call matches against a set mock responses, 
/// they will be executed in order for each subsequent call to HCHttpCallPerform(). When the 
/// last matching mock response is hit, the last matching mock response will be repeated on 
/// each subsequent call to HCHttpCallPerform().
/// </summary>
/// <param name="call">This HC_CALL_HANDLE that represents the mock that has been configured accordingly 
/// using HCHttpCallResponseSet*(), and optionally HCHttpCallRequestSetUrl() and HCHttpCallRequestSetRequestBodyString().</param>
HC_API void HC_CALLING_CONV
HCSettingsAddMockCall(
    _In_ HC_CALL_HANDLE call
    );

/// <summary>
/// Removes and cleans up all mock calls added by HCSettingsAddMockCall
/// </summary>
HC_API void HC_CALLING_CONV
HCSettingsClearMockCalls();

//
// HCHttpCall APIs
//

/// <summary>
/// </summary>
/// <param name="call"></param>
HC_API void HC_CALLING_CONV
HCHttpCallCreate(
    _Out_ HC_CALL_HANDLE* call
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="completionRoutineContext"></param>
/// <param name="completionRoutine"></param>
HC_API void HC_CALLING_CONV
HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ void* completionRoutineContext,
    _In_ HCHttpCallPerformCompletionRoutine completionRoutine
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
HC_API void HC_CALLING_CONV
HCHttpCallCleanup(
    _In_ HC_CALL_HANDLE call
    );


//
// HCHttpCallRequest APIs
//

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="method"></param>
/// <param name="url"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T method,
    _In_ PCSTR_T url
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="method"></param>
/// <param name="url"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetUrl(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR_T* method,
    _Outptr_ PCSTR_T* url
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="requestBodyString"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T requestBodyString
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="requestBodyString"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR_T* requestBodyString
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="headerName"></param>
/// <param name="headerValue"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _In_ PCSTR_T headerValue
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="headerName"></param>
/// <param name="headerValue"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T* headerValue
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="numHeaders"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="headerIndex"></param>
/// <param name="headerName"></param>
/// <param name="headerValue"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR_T* headerName,
    _Out_ PCSTR_T* headerValue
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="retryAllowed"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRetryAllowed(
    _In_ HC_CALL_HANDLE call,
    _In_ bool retryAllowed
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="retryAllowed"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRetryAllowed(
    _In_ HC_CALL_HANDLE call,
    _Out_ bool* retryAllowed
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="timeoutInSeconds"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="timeoutInSeconds"></param>
HC_API void HC_CALLING_CONV
HCHttpCallRequestGetTimeout(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutInSeconds
    );


// 
// HCHttpCallResponse APIs
// 

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="responseString"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetResponseString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR_T* responseString
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="responseString"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetResponseString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T responseString
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="statusCode"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* statusCode
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="statusCode"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t statusCode
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="errorCode"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetErrorCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* errorCode
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="errorCode"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetErrorCode(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t errorCode
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="errorMessage"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetErrorMessage(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR_T* errorMessage
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="errorMessage"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetErrorMessage(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T errorMessage
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="headerName"></param>
/// <param name="headerValue"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T* headerValue
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="numHeaders"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="headerIndex"></param>
/// <param name="headerName"></param>
/// <param name="headerValue"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR_T* headerName,
    _Out_ PCSTR_T* headerValue
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="headerName"></param>
/// <param name="headerValue"></param>
HC_API void HC_CALLING_CONV
HCHttpCallResponseSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T headerValue
    );

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

