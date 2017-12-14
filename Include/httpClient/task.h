// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////
// Task APIs
// 

/// <summary>
/// The callback definition used by HCTaskCreate to execute the task.
/// </summary>
/// <param name="context">The context passed to this callback</param>
/// <param name="taskHandle">The handle to the task</param>
typedef HC_RESULT
(HC_CALLING_CONV* HC_TASK_EXECUTE_FUNC)(
    _In_opt_ void* context,
    _In_ HC_TASK_HANDLE taskHandle
    );

/// <summary>
/// The callback definition used by HCTaskCreate to write results.
/// </summary>
/// <param name="context">The context passed to this callback</param>
/// <param name="taskHandle">The handle to the task</param>
typedef HC_RESULT
(HC_CALLING_CONV* HC_TASK_WRITE_RESULTS_FUNC)(
    _In_opt_ void* writeContext,
    _In_ HC_TASK_HANDLE taskHandle,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext
    );

/// <summary>
/// The task event type
/// </summary>
typedef enum HC_TASK_EVENT_TYPE
{
    /// <summary>
    /// The task is in a pending task and has not started executing 
    /// </summary>
    HC_TASK_EVENT_PENDING,

    /// <summary>
    /// The task has started executing but has not yet completed execution
    /// </summary>
    HC_TASK_EVENT_EXECUTE_STARTED,

    /// <summary>
    /// The task has completed executing
    /// </summary>
    HC_TASK_EVENT_EXECUTE_COMPLETED
} HC_TASK_EVENT_TYPE;

/// <summary>
/// The callback definition used by HCSetTaskEventHandler that's raised when a task changes state (pending, executing, completed).
/// </summary>
/// <param name="context">The context passed to this callback</param>
/// <param name="eventType">The event type for this callback</param>
/// <param name="taskHandle">The handle to the task</param>
typedef HC_RESULT
(HC_CALLING_CONV* HC_TASK_EVENT_FUNC)(
    _In_opt_ void* context,
    _In_ HC_TASK_EVENT_TYPE eventType,
    _In_ HC_TASK_HANDLE taskHandle
    );

/// <summary>
/// Sets the callback that is called when when task changes state (pending, executing, completed)
/// </summary>
/// <param name="taskSubsystemId">
/// The task subsystem ID to assign to this task.  This is the ID of the caller's subsystem.
/// If this isn't needed or unknown, just pass in HC_SUBSYSTEM_ID_GAME.
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// can each expose thier own version of ProcessNextPendingTask() and 
/// ProcessNextCompletedTask() APIs that operate independently.
/// </param>
/// <param name="context">The context passed to the event handler whenever it is called</param>
/// <param name="taskEventFunc">The function pointer for the event handler.  Set it to nullptr to disable</param>
/// <param name="eventHandle">Handle to the event handler.  Use this to remove the handler using HCRemoveTaskEventHandler</param>
HC_API HC_RESULT HC_CALLING_CONV
HCAddTaskEventHandler(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_opt_ void* context,
    _In_opt_ HC_TASK_EVENT_FUNC taskEventFunc,
    _Out_opt_ HC_TASK_EVENT_HANDLE* eventHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Removes the callback that is called when when task changes state (pending, executing, completed)
/// </summary>
/// <param name="eventHandle">Handle to the event handler.  Use this to remove the handler using HCRemoveTaskEventHandler</param>
HC_API HC_RESULT HC_CALLING_CONV
HCRemoveTaskEventHandler(
    _Out_ HC_TASK_EVENT_HANDLE eventHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Create a new async task by passing in 3 callbacks and their associated contexts which
/// are passed to each corresponding callback.
///
/// The executionRoutine callback performs the task itself and may take time to complete.
/// The writeResultsRoutine callback has the knowledge to cast and call the 'void* completionRoutine'
/// callback based on a task specific callback definition.
///
/// The executionRoutine callback is called when
/// HCTaskProcessNextPendingTask() is called. It is recommended the app calls HCTaskProcessNextPendingTask()
/// in a background thread.
///
/// Right before the executionRoutine callback is finished, the executionRoutine should
/// call HCTaskSetCompleted() to mark the task as completed and ready to return results.
///
/// When the task is completed, the completionRoutine is called on the
/// thread that calls HCTaskProcessNextCompletedTask().  This enables the caller to execute
/// the callback on a specific thread to avoid the need to marshal data to a app thread
/// from a background thread.
///
/// HCTaskProcessNextCompletedTask(taskGroupId) will only process completed tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </summary>
/// <param name="taskSubsystemId">
/// The task subsystem ID to assign to this task.  This is the ID of the caller's subsystem.
/// If this isn't needed or unknown, just pass in HC_SUBSYSTEM_ID_GAME.
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// can each expose thier own version of ProcessNextPendingTask() and 
/// ProcessNextCompletedTask() APIs that operate independently.
/// </param>
/// <param name="taskGroupId">
/// The task group ID to assign to this task.  The ID is defined by the caller and can be any number.
/// HCTaskProcessNextCompletedTask(taskSubsystemId, taskGroupId) will only process completed tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </param>
/// <param name="executionRoutine">
/// The executionRoutine callback performs the task itself and may take time to complete.
/// Right before the executionRoutine callback is finished, the executionRoutine should
/// call HCTaskSetCompleted() to mark the task as completed to return results.
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
/// This is called on the app thread that calls HCTaskProcessNextCompletedTask().
/// This enables the caller to execute the callback on a specific thread to avoid the
/// need to marshal data to a app thread from a background thread.
/// </param>
/// <param name="completionRoutineContext">
/// The context passed to the completionRoutine callback
/// </param>
/// <param name="taskHandle">
/// Optionally will return the handle of the task which can be passed to other APIs such as HCTaskIsCompleted()
/// </param>
HC_API HC_RESULT HC_CALLING_CONV
HCTaskCreate(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_ HC_TASK_EXECUTE_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_WRITE_RESULTS_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _Out_opt_ HC_TASK_HANDLE* taskHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Calls the executionRoutine callback for the next pending task. It is recommended 
/// the app calls HCTaskProcessNextPendingTask() in a background thread.
/// </summary>
/// <param name="taskSubsystemId">
/// The task subsystem ID to assign to this task.  This is the ID of the caller's subsystem.
/// If this isn't needed or unknown, just pass in HC_SUBSYSTEM_ID_GAME.
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// can each expose thier own version of ProcessNextPendingTask() and 
/// ProcessNextCompletedTask() APIs that operate independently.
/// </param>
HC_API HC_RESULT HC_CALLING_CONV
HCTaskProcessNextPendingTask(_In_ HC_SUBSYSTEM_ID taskSubsystemId) HC_NOEXCEPT;

/// <summary>
/// Calls the completionRoutine callback for the next task that is completed.  
/// This enables the caller to execute the callback on a specific thread to 
/// avoid the need to marshal data to a app thread from a background thread.
/// 
/// HCTaskProcessNextCompletedTask will only process completed tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </summary>
/// <param name="taskSubsystemId">
/// HCTaskProcessNextCompletedTask will only process completed tasks that have a
/// matching subsystem ID. 
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// can each expose thier own version of ProcessNextPendingTask() and 
/// ProcessNextCompletedTask() APIs that operate independently.
/// </param>
/// <param name="taskGroupId">
/// HCTaskProcessNextCompletedTask will only process completed tasks that have a
/// matching taskGroupId.  This enables the caller to split the where results are
/// returned between between a set of app threads.  If this isn't needed, just pass in 0.
/// </param>
HC_API HC_RESULT HC_CALLING_CONV
HCTaskProcessNextCompletedTask(_In_ HC_SUBSYSTEM_ID taskSubsystemId, _In_ uint64_t taskGroupId) HC_NOEXCEPT;

/// <summary>
/// Called by async task's executionRoutine when the results are completed.  This will mark the task as
/// completed so the app can call HCTaskProcessNextCompletedTask() to get the results in
/// the completionRoutine callback.
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
HC_API HC_RESULT HC_CALLING_CONV
HCTaskSetCompleted(
    _In_ HC_TASK_HANDLE taskHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Returns the size of the pending task queue for a specific task subsystem and task group ID
/// </summary>
/// <param name="taskSubsystemId">
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// </param>
/// <returns>Returns the size of the pending task queue for a specific task subsystem and task group ID</returns>
HC_API uint64_t HC_CALLING_CONV
HCTaskGetPendingTaskQueueSize(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId
    ) HC_NOEXCEPT;

/// <summary>
/// Returns the size of the completed task queue for a specific task subsystem and task group ID
/// </summary>
/// <param name="taskSubsystemId">
/// This is used to subdivide results so each subsystem (XSAPI, XAL, Mixer, etc) 
/// </param>
/// <param name="taskGroupId">
/// This enables the caller to split the where results are returned between between a set of app threads.  
/// If this isn't needed, just pass in 0.
/// </param>
/// <returns>Returns the size of the completed task queue for a specific task subsystem and task group ID</returns>
HC_API uint64_t HC_CALLING_CONV
HCTaskGetCompletedTaskQueueSize(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId, 
    _In_ uint64_t taskGroupId
    ) HC_NOEXCEPT;

/// <summary>
/// Returns if the task's result is completed and ready to return results
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
/// <returns>Returns true if the task's result is completed</returns>
HC_API bool HC_CALLING_CONV
HCTaskIsCompleted(
    _In_ HC_TASK_HANDLE taskHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Returns the task's subsystem ID 
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
/// <returns>Returns the task's subsystem ID</returns>
HC_API HC_SUBSYSTEM_ID HC_CALLING_CONV
HCTaskGetSubsystemId(
    _In_ HC_TASK_HANDLE taskHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Returns the task's task group ID
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate</param>
/// <returns>Returns the task's task group ID</returns>
HC_API uint64_t HC_CALLING_CONV
HCTaskGetTaskGroupId(
    _In_ HC_TASK_HANDLE taskHandle
    ) HC_NOEXCEPT;

/// <summary>
/// Wait until a specific task is completed.
/// When the async task's executionRoutine has finished the task, it should call HCTaskSetCompleted() which will
/// mark the task as completed
/// </summary>
/// <param name="taskHandle">Handle to task returned by HCTaskCreate()</param>
/// <param name="timeoutInMilliseconds">Timeout in milliseconds</param>
/// <returns>Returns true if completed, false if timed out.</returns>
HC_API bool HC_CALLING_CONV
HCTaskWaitForCompleted(
    _In_ HC_TASK_HANDLE taskHandle,
    _In_ uint32_t timeoutInMilliseconds
    ) HC_NOEXCEPT;

#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)

