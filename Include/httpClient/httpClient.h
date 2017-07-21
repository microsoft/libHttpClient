// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include "types.h"

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
/// allocations to thier own memory manager. This must be called before HCGlobalInitialize() 
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
/// <returns>The version of the library in the format of release_year.release_month.date.rev.  
/// For example, 2017.07.20170710.01</returns>
HC_API void HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR_T* version);

/// <summary>
/// </summary>
/// <param name="call"></param>
/// <param name="taskHandle"></param>
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
// Task APIs
// 

/// <summary>
/// The callback definition used by HCTaskCreate.
/// </summary>
/// <param name="context">The context passed to this callback</param>
/// <param name="taskHandle">The handle to the task</param>
typedef void
(HC_CALLING_CONV* HC_ASYNC_OP_FUNC)(
    _In_opt_ void* context,
    _In_ HC_TASK_HANDLE taskHandle
    );

/// <summary>
/// Create a new async task by passing in 3 callbacks and their associated contexts which
/// are passed to each corresponding callback.
///
/// The executionRoutine callback performs the task itself and may take time to complete.
/// The writeResultsRoutine callback has the knowledge to cast and call the 'void* completionRoutine'
/// callback based on a task specific callback definition.
///
/// If executeNow is true, the executionRoutine callback will be called immediately 
/// inside HCTaskCreate().  This is useful if the execution of the async task is quick and just
/// needs to kick off other async tasks such as an HTTP call.
/// 
/// If executeNow is false, then the executionRoutine callback is called when
/// HCTaskProcessNextPendingTask() is called. Setting executeNow to false is useful for 
/// tasks that are long running. It is recommended the app calls HCTaskProcessNextPendingTask()
/// in a background thread.
///
/// Right before the executionRoutine callback is finished, the executionRoutine should
/// call HCTaskSetResultReady() to mark the task as ready to return results.
///
/// When the task is ready to return results, the completionRoutine is called on the
/// thread that calls HCTaskProcessNextResultReadyTask().  This enables the caller to execute
/// the callback on a specific thread to avoid the need to marshal data to a app thread
/// from a background thread.
///
/// HCTaskProcessNextResultReadyTask(taskGroupId) will only process ready tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </summary>
/// <param name="taskGroupId">
/// The task group ID to assign to this task.
/// HCTaskProcessNextResultReadyTask(taskGroupId) will only process ready tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </param>
/// <param name="executionRoutine">
/// The executionRoutine callback performs the task itself and may take time to complete.
/// Right before the executionRoutine callback is finished, the executionRoutine should
/// call HCTaskSetResultReady() to mark the task as ready to return results.
/// </param>
/// <param name="executionRoutineContext">
/// The context passed to the executionRoutine callback
/// </param>
/// <param name="writeResultsRoutine">
/// The writeResultsRoutine callback has the knowledge to cast and call the 'void* completionRoutine'
/// callback based on a task specific callback definition.
/// </param>
/// <param name="writeResultsRoutineContext">
/// The context passed to the writeResultsRoutine callback
/// </param>
/// <param name="completionRoutine">
/// A task specific callback that return results to the caller.
/// This is called on the app thread that calls HCTaskProcessNextResultReadyTask().
/// This enables the caller to execute the callback on a specific thread to avoid the
/// need to marshal data to a app thread from a background thread.
/// </param>
/// <param name="completionRoutineContext">
/// The context passed to the completionRoutine callback
/// </param>
/// <param name="executeNow">
/// If executeNow is true, the executionRoutine callback will be called immediately
/// inside HCTaskCreate().  This is useful if the execution of the async task is quick and just
/// needs to kick off other async tasks such as an HTTP call.
/// 
/// If executeNow is false, then the executionRoutine callback is called when
/// HCTaskProcessNextPendingTask() is called. Setting executeNow to false is useful for
/// tasks that are long running. It is recommended the app calls HCTaskProcessNextPendingTask()
/// in a background thread.
/// </param>
HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCTaskCreate(
    _In_ uint32_t taskGroupId,
    _In_ HC_ASYNC_OP_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_ HC_ASYNC_OP_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _In_ bool executeNow
    );

/// <summary>
/// Returns if there is any async task that is in a pending state that hasn't yet been executed.
/// HCTaskProcessNextPendingTask() will execute the next pending task.
/// </summary>
/// <returns>Returns true if the async op is pending</returns>
HC_API bool HC_CALLING_CONV
HCTaskIsTaskPending();

#if UWP_API
/// <summary>
/// Returns a handle that can be used to wait until there is a pending task that hasn't yet be executed.
/// HCTaskProcessNextPendingTask() will execute the next pending task.
/// </summary>
HC_API HANDLE HC_CALLING_CONV
HCTaskGetPendingHandle();

/// <summary>
/// Returns a handle that can be used to wait until there is a completed task that hasn't 
/// yet be returned results to the caller. HCTaskProcessNextResultReadyTask() will execute the 
/// next completed task 
/// </summary>
HC_API HANDLE HC_CALLING_CONV
HCTaskGetCompletedHandle();
#endif

/// <summary>
/// Calls the executionRoutine callback for the next pending task. It is recommended 
/// the app calls HCTaskProcessNextPendingTask() in a background thread.
/// </summary>
HC_API void HC_CALLING_CONV
HCTaskProcessNextPendingTask();

/// <summary>
/// Called by async task when the results are ready.  This will mark the task as
/// completed so the app can call HCTaskProcessNextResultReadyTask() to get the results in
/// the completionRoutine callback.
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
HC_API void HC_CALLING_CONV
HCTaskSetResultReady(
    _In_ HC_TASK_HANDLE taskHandle
    );

/// <summary>
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
HC_API bool HC_CALLING_CONV
HCTaskIsResultReady(
    _In_ HC_TASK_HANDLE taskHandle
    );

/// <summary>
/// Wait until the results are ready
/// When the async task is done, it should call HCTaskSetResultReady() which will 
/// mark the task as ready
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
/// <param name="timeoutInMilliseconds">Timeout in milliseconds.</param>
HC_API void HC_CALLING_CONV
HCTaskWaitForResultReady(
    _In_ HC_TASK_HANDLE taskHandle,
    _In_ uint32_t timeoutInMilliseconds
    );

/// <summary>
/// Calls the completionRoutine callback for the next task that is ready.  
/// This enables the caller to execute the callback on a specific thread to 
/// avoid the need to marshal data to a app thread from a background thread.
/// 
/// HCTaskProcessNextResultReadyTask will only process ready tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </summary>
/// <param name="taskGroupId">
/// HCTaskProcessNextResultReadyTask will only process ready tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </param>
HC_API void HC_CALLING_CONV
HCTaskProcessNextResultReadyTask(_In_ uint32_t taskGroupId);


/////////////////////////////////////////////////////////////////////////////////////////
// Settings APIs
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
/// </summary>
/// <param name="logLevel"></param>
HC_API void HC_CALLING_CONV
HCSettingsSetLogLevel(
    _In_ HC_LOG_LEVEL logLevel
    );

/// <summary>
/// </summary>
/// <param name="logLevel"></param>
HC_API void HC_CALLING_CONV
HCSettingsGetLogLevel(
    _Out_ HC_LOG_LEVEL* logLevel
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

/////////////////////////////////////////////////////////////////////////////////////////
// HttpCall APIs
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
/// <param name="taskGroupId"></param>
/// <param name="call"></param>
/// <param name="completionRoutineContext"></param>
/// <param name="completionRoutine"></param>
HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCHttpCallPerform(
    _In_ uint32_t taskGroupId,
    _In_ HC_CALL_HANDLE call,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCHttpCallPerformCompletionRoutine completionRoutine
    );

/// <summary>
/// </summary>
/// <param name="call"></param>
HC_API void HC_CALLING_CONV
HCHttpCallCleanup(
    _In_ HC_CALL_HANDLE call
    );


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallRequest APIs
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


/////////////////////////////////////////////////////////////////////////////////////////
// HttpCallResponse APIs
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

