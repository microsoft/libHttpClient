// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

/////////////////////////////////////////////////////////////////////////////////////////
// Tracing APIs
//

/// <summary>
/// Diagnostic level used by tracing
/// </summary>
typedef enum HCTraceLevel
{
    /// <summary>
    /// No tracing
    /// </summary>
    HCTraceLevel_Off = 0,

    /// <summary>
    /// Trace only errors
    /// </summary>
    HCTraceLevel_Error = 1,

    /// <summary>
    /// Trace warnings and errors
    /// </summary>
    HCTraceLevel_Warning = 2,

    /// <summary>
    /// Trace important, warnings and errors
    /// </summary>
    HCTraceLevel_Important = 3,

    /// <summary>
    /// Trace info, important, warnings and errors
    /// </summary>
    HCTraceLevel_Information = 4,

    /// <summary>
    /// Trace everything
    /// </summary>
    HCTraceLevel_Verbose = 5,
} HCTraceLevel;

/// <summary>
/// Sets the trace level for the library.  Traces are sent the debug output
/// </summary>
/// <param name="traceLevel">Trace level</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCSettingsSetTraceLevel(
    _In_ HCTraceLevel traceLevel
    ) HC_NOEXCEPT;

/// <summary>
/// Gets the trace level for the library
/// </summary>
/// <param name="traceLevel">Trace level</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCSettingsGetTraceLevel(
    _Out_ HCTraceLevel* traceLevel
    ) HC_NOEXCEPT;

/// <summary>
/// Register callback for tracing so that the client can merge tracing into their
/// own traces. 
/// </summary>
typedef void (HCTraceCallback)(
    _In_ UTF8CSTR areaName,
    _In_ enum HCTraceLevel level,
    _In_ uint32_t threadId,
    _In_ uint64_t timestamp,
    _In_ UTF8CSTR message
    );

/// <summary>
/// Set client callback for tracing 
/// </summary>
/// <param name="callback">Trace callback</param>
STDAPI_(void) HCTraceSetClientCallback(_In_opt_ HCTraceCallback* callback);

/// <summary>
/// Set client callback for tracing 
/// </summary>
/// <param name="callback">Trace callback</param>
STDAPI_(void) HCTraceSetTraceToDebugger(_In_ bool traceToDebugger);

