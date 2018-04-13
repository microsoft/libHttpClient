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


//------------------------------------------------------------------------------
// Trace macros
//------------------------------------------------------------------------------
// These are the macros to be used to log
// These macros are always defined but will compile to nothing if
// HC_TRACE_BUILD_LEVEL is not high enough

#if HC_TRACE_ENABLE
    #define HC_TRACE_MESSAGE(area, level, format, ...) HCTraceImplMessage(&HC_PRIVATE_TRACE_AREA_NAME(area), (level), format, ##__VA_ARGS__)
    #ifdef __cplusplus
        #define HC_TRACE_SCOPE(area, level) auto tsh = HCTraceImplScopeHelper{ &HC_PRIVATE_TRACE_AREA_NAME(area), level, HC_FUNCTION }
    #else
        #define HC_TRACE_SCOPE(area, level)
    #endif
#else
    #define HC_TRACE_MESSAGE(area, level, ...)
    #define HC_TRACE_MESSAGE_WITH_LOCATION(area, level, format, ...)
    #define HC_TRACE_SCOPE(area, level)
#endif

#if HC_TRACE_ERROR_ENABLE
    #define HC_TRACE_ERROR(area, msg, ...)  HC_TRACE_MESSAGE(area, HCTraceLevel_Error, msg, ##__VA_ARGS__)
    #define HC_TRACE_ERROR_HR(area, failedHr, msg) HC_TRACE_ERROR(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
    #define HC_TRACE_ERROR(area, msg, ...)
    #define HC_TRACE_ERROR_HR(area, failedHr, msg)
#endif

#if HC_TRACE_WARNING_ENABLE
    #define HC_TRACE_WARNING(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel_Warning, msg, ##__VA_ARGS__)
    #define HC_TRACE_WARNING_HR(area, failedHr, msg) HC_TRACE_WARNING(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
    #define HC_TRACE_WARNING(area, msg, ...)
    #define HC_TRACE_WARNING_HR(area, failedHr, msg)
#endif

#if HC_TRACE_IMPORTANT_ENABLE
    #define HC_TRACE_IMPORTANT(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel_Important, msg, ##__VA_ARGS__)
    #define HC_TRACE_SCOPE_IMPORTANT(area) HC_TRACE_SCOPE(area, HCTraceLevel_Important)
#else
    #define HC_TRACE_IMPORTANT(area, msg, ...)
    #define HC_TRACE_SCOPE_IMPORTANT(area)
#endif

#if HC_TRACE_INFORMATION_ENABLE
    #define HC_TRACE_INFORMATION(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel_Information, msg, ##__VA_ARGS__)
    #define HC_TRACE_SCOPE_INFORMATION(area) HC_TRACE_SCOPE(area, HCTraceLevel_Information)
#else
    #define HC_TRACE_INFORMATION(area, msg, ...)
    #define HC_TRACE_SCOPE_INFORMATION(area)
#endif

#if HC_TRACE_VERBOSE_ENABLE
    #define HC_TRACE_VERBOSE(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel_Verbose, msg, ##__VA_ARGS__)
    #define HC_TRACE_SCOPE_VERBOSE(area) HC_TRACE_SCOPE(area, HCTraceLevel_Verbose)
#else
    #define HC_TRACE_VERBOSE(area, msg, ...)
    #define HC_TRACE_SCOPE_VERBOSE(area)
#endif
