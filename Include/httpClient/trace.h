// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#if !defined(__cplusplus)
    #error C++11 required
#endif

#pragma once

#include <httpClient/pal.h>

#ifndef HC_TRACE_BUILD_LEVEL
#define HC_TRACE_BUILD_LEVEL HC_PRIVATE_TRACE_LEVEL_VERBOSE
#endif

extern "C"
{

/////////////////////////////////////////////////////////////////////////////////////////
// Tracing APIs
//

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------
// All macros used for configuration are always defined. Clients can define
// their own values for these macros to control tracing behavior
//
// HC_TRACE_BUILD_LEVEL [trace level (0-5)]
//     controls the maximum level of verbosity that will be built in the
//     executable. To control verbosity at runtime see TraceArea. Set to 0 to 
//     completely disable tracing
//
// HC_TRACE_TO_DEBUGGER [0,1]
//     controls if trace will output using OutputDebugString
//     only has an effect when building trace.cpp

// These defines are not meant to be used by clients at runtime, the
// HCTraceLevel enum should be used instead
#define HC_PRIVATE_TRACE_LEVEL_OFF 0
#define HC_PRIVATE_TRACE_LEVEL_ERROR 1
#define HC_PRIVATE_TRACE_LEVEL_WARNING 2
#define HC_PRIVATE_TRACE_LEVEL_IMPORTANT 3
#define HC_PRIVATE_TRACE_LEVEL_INFORMATION 4
#define HC_PRIVATE_TRACE_LEVEL_VERBOSE 5

#ifndef HC_TRACE_BUILD_LEVEL
#define HC_TRACE_BUILD_LEVEL HC_PRIVATE_TRACE_LEVEL_VERBOSE
#endif

#ifndef HC_TRACE_TO_DEBUGGER
#define HC_TRACE_TO_DEBUGGER 1
#endif


//------------------------------------------------------------------------------
// Level enabled macros
//------------------------------------------------------------------------------
// These macros can be used to easily check if a given trace level will be
// included in the build (for example to avoid compiling helper code for a
// trace).
// These macros are always defined
//
// HC_TRACE_ENABLE [0,1]
// HC_TRACE_ERROR_ENABLE [0,1]
// HC_TRACE_WARNING_ENABLE [0,1]
// HC_TRACE_IMPORTANT_ENABLE [0,1]
// HC_TRACE_INFORMATION_ENABLE [0,1]
// HC_TRACE_VERBOSE_ENABLE [0,1]
//
// these should not be manually modified, they are automatically set based on
// the values of HC_TRACE_BUILD_LEVEL

#if HC_TRACE_BUILD_LEVEL > HC_PRIVATE_TRACE_LEVEL_OFF
    #define HC_TRACE_ENABLE 1
#else
    #define HC_TRACE_ENABLE 0
#endif

#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_ERROR
    #define HC_TRACE_ERROR_ENABLE 1
#else
    #define HC_TRACE_ERROR_ENABLE 0
#endif

#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_WARNING
    #define HC_TRACE_WARNING_ENABLE 1
#else
    #define HC_TRACE_WARNING_ENABLE 0
#endif

#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_IMPORTANT
    #define HC_TRACE_IMPORTANT_ENABLE 1
#else
    #define HC_TRACE_IMPORTANT_ENABLE 0
#endif


#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_INFORMATION
    #define HC_TRACE_INFORMATION_ENABLE 1
#else
    #define HC_TRACE_INFORMATION_ENABLE 0
#endif

#if HC_TRACE_BUILD_LEVEL >= HC_PRIVATE_TRACE_LEVEL_VERBOSE
    #define HC_TRACE_VERBOSE_ENABLE 1
#else
    #define HC_TRACE_VERBOSE_ENABLE 0
#endif


/// <summary>
/// Diagnostic level used by tracing.
/// </summary>
enum class HCTraceLevel : uint32_t
{
    /// <summary>
    /// No tracing.
    /// </summary>
    Off = HC_PRIVATE_TRACE_LEVEL_OFF,

    /// <summary>
    /// Trace only errors.
    /// </summary>
    Error = HC_PRIVATE_TRACE_LEVEL_ERROR,

    /// <summary>
    /// Trace warnings and errors.
    /// </summary>
    Warning = HC_PRIVATE_TRACE_LEVEL_WARNING,

    /// <summary>
    /// Trace important, warnings and errors.
    /// </summary>
    Important = HC_PRIVATE_TRACE_LEVEL_IMPORTANT,

    /// <summary>
    /// Trace info, important, warnings and errors.
    /// </summary>
    Information = HC_PRIVATE_TRACE_LEVEL_INFORMATION,

    /// <summary>
    /// Trace everything.
    /// </summary>
    Verbose = HC_PRIVATE_TRACE_LEVEL_VERBOSE,
};

/// <summary>
/// Initialize tracing for the library.
/// </summary>
/// <remarks>
/// This function is implicitly called during HCInitialize. Initialization is reference counted, and
/// multiple calls to HCTraceInit and HCTraceCleanup will not interfere with each other as long as
/// each call to HCTraceInit is paired with exactly one call to HCTraceCleanup.
/// </remarks>
void HCTraceInit() noexcept;

/// <summary>
/// Clean up tracing for the library.
/// </summary>
/// <remarks>
/// This function is implicitly called during HCCleanup. See HCTraceInit for remarks on reference
/// counting and multiple calls to these functions.
/// </remarks>
void HCTraceCleanup() noexcept;

/// <summary>
/// Sets the trace level for the library.  Traces are sent the debug output.
/// </summary>
/// <param name="traceLevel">Trace level.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCSettingsSetTraceLevel(
    _In_ HCTraceLevel traceLevel
    ) noexcept;

/// <summary>
/// Gets the trace level for the library.
/// </summary>
/// <param name="traceLevel">Trace level.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, E_INVALIDARG, or E_FAIL.</returns>
STDAPI HCSettingsGetTraceLevel(
    _Out_ HCTraceLevel* traceLevel
    ) noexcept;

/// <summary>
/// Register callback for tracing so that the client can merge tracing into their
/// own traces. 
/// </summary>
typedef void (CALLBACK HCTraceCallback)(
    _In_z_ const char* areaName,
    _In_ HCTraceLevel level,
    _In_ uint64_t threadId,
    _In_ uint64_t timestamp,
    _In_z_ const char* message
    );

/// <summary>
/// Set client callback for tracing.
/// </summary>
/// <param name="callback">Trace callback.</param>
/// <returns></returns>
STDAPI_(void) HCTraceSetClientCallback(_In_opt_ HCTraceCallback* callback) noexcept;

/// <summary>
/// Sets or unsets if the trace is sent to the debugger.
/// </summary>
/// <param name="traceToDebugger">If True, sends the trace to the debugger.</param>
/// <returns></returns>
STDAPI_(void) HCTraceSetTraceToDebugger(_In_ bool traceToDebugger) noexcept;


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
    #define HC_TRACE_ERROR(area, msg, ...)  HC_TRACE_MESSAGE(area, HCTraceLevel::Error, msg, ##__VA_ARGS__)
    #define HC_TRACE_ERROR_HR(area, failedHr, msg) HC_TRACE_ERROR(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
    #define HC_TRACE_ERROR(area, msg, ...)
    #define HC_TRACE_ERROR_HR(area, failedHr, msg)
#endif

#if HC_TRACE_WARNING_ENABLE
    #define HC_TRACE_WARNING(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel::Warning, msg, ##__VA_ARGS__)
    #define HC_TRACE_WARNING_HR(area, failedHr, msg) HC_TRACE_WARNING(area, "%hs (hr=0x%08x)", msg, failedHr)
#else
    #define HC_TRACE_WARNING(area, msg, ...)
    #define HC_TRACE_WARNING_HR(area, failedHr, msg)
#endif

#if HC_TRACE_IMPORTANT_ENABLE
    #define HC_TRACE_IMPORTANT(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel::Important, msg, ##__VA_ARGS__)
    #define HC_TRACE_SCOPE_IMPORTANT(area) HC_TRACE_SCOPE(area, HCTraceLevel::Important)
#else
    #define HC_TRACE_IMPORTANT(area, msg, ...)
    #define HC_TRACE_SCOPE_IMPORTANT(area)
#endif

#if HC_TRACE_INFORMATION_ENABLE
    #define HC_TRACE_INFORMATION(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel::Information, msg, ##__VA_ARGS__)
    #define HC_TRACE_SCOPE_INFORMATION(area) HC_TRACE_SCOPE(area, HCTraceLevel::Information)
#else
    #define HC_TRACE_INFORMATION(area, msg, ...)
    #define HC_TRACE_SCOPE_INFORMATION(area)
#endif

#if HC_TRACE_VERBOSE_ENABLE
    #define HC_TRACE_VERBOSE(area, msg, ...) HC_TRACE_MESSAGE(area, HCTraceLevel::Verbose, msg, ##__VA_ARGS__)
    #define HC_TRACE_SCOPE_VERBOSE(area) HC_TRACE_SCOPE(area, HCTraceLevel::Verbose)
#else
    #define HC_TRACE_VERBOSE(area, msg, ...)
    #define HC_TRACE_SCOPE_VERBOSE(area)
#endif

//------------------------------------------------------------------------------
// Trace area macros
//------------------------------------------------------------------------------
// These macros are used to set up areas for tracing
// These macros are always defined but will compile to nothing if trace is
// disabled

#if HC_TRACE_ENABLE
    // Defines an area for tracing:
    // name is used as a prefix for all traces in the area
    // verbosity sets the maximum verbosity that will be traced within this area
    // verbosity can be tweaked at runtime by calling HC_TRACE_SET_VERBOSITY(area,
    // level)
    // since this defines a global variable, it should be only used from a .cpp file
    // and each area should be defined only once
    #define HC_DEFINE_TRACE_AREA(area, verbosity) struct HCTraceImplArea HC_PRIVATE_TRACE_AREA_NAME(area) = { #area, (verbosity) }

    // Declares a trace area. Since DEFINE_TRACE_AREA can only be used once and in a
    // .cpp, this allows to trace in an already defined area from another file
    #define HC_DECLARE_TRACE_AREA(area) extern struct HCTraceImplArea HC_PRIVATE_TRACE_AREA_NAME(area)

    // Access to the verbosity property of the area for runtime control
    #define HC_TRACE_SET_VERBOSITY(area, level) HCTraceImplSetAreaVerbosity(&HC_PRIVATE_TRACE_AREA_NAME(area), level)

    #define HC_TRACE_GET_VERBOSITY(area) HCTraceImplGetAreaVerbosity(&HC_PRIVATE_TRACE_AREA_NAME(area))
#else
    #define HC_DEFINE_TRACE_AREA(name, verbosity)
    #define HC_DECLARE_TRACE_AREA(name)
    #define HC_TRACE_SET_VERBOSITY(area, level)
    #define HC_TRACE_GET_VERBOSITY(area) HCTraceLevel::Off
#endif

//------------------------------------------------------------------------------
// Platform Hooks
//------------------------------------------------------------------------------
typedef uint64_t (CALLBACK HCTracePlatformThisThreadIdCallback)(void*);
typedef void (CALLBACK HCTracePlatformWriteMessageToDebuggerCallback)(char const*, HCTraceLevel, char const*, void*);

/// <summary>
/// Sets the Platform Callbacks.
/// </summary>
/// <param name="threadIdCallback">The thread ID callback.</param>
/// <param name="threadIdContext">The thread ID context.</param>
/// <param name="writeToDebuggerCallback">The write to debbugger callback.</param>
/// <param name="writeToDebuggerContext">The write to debbugger context.</param>
/// <returns>Result code for this API operation.  Possible values are S_OK, or E_HC_NOT_INITIALISED.</returns>
STDAPI HCTraceSetPlatformCallbacks(
    _In_ HCTracePlatformThisThreadIdCallback* threadIdCallback,
    _In_opt_ void* threadIdContext,
    _In_ HCTracePlatformWriteMessageToDebuggerCallback* writeToDebuggerCallback,
    _In_opt_ void* writeToDebuggerContext
    ) noexcept;

//------------------------------------------------------------------------------
// Implementation
//------------------------------------------------------------------------------
// These symbols are always built even if trace is disabled, but note that no
// trace areas will be defined so they should never be used directly
// the reason why they are always built is to avoid issues with different
// compilation units setting different values for HC_TRACE_BUILD_LEVEL
// the linker will discard everything if tracing is disabled because there will
// be no references

// DO NOT USE THESE SYMBOLS DIRECTLY

#define HC_PRIVATE_TRACE_AREA_NAME(area) g_trace##area
#define HC_FUNCTION __FUNCTION__

typedef struct HCTraceImplArea
{
    char const* const Name;
    HCTraceLevel Verbosity;
} HCTraceImplArea;

/// <summary>
/// Set the verbosity level of an trace area. 
/// </summary>
/// <param name="area">The trace area.</param>
/// <param name="verbosity">The verbosity level.</param>
/// <remarks></remarks>
/// <returns>This should be accessed through macros, such as HC_TRACE_SET_VERBOSITY, rather than called directly.</returns>
EXTERN_C inline
void STDAPIVCALLTYPE HCTraceImplSetAreaVerbosity(
    struct HCTraceImplArea* area,
    HCTraceLevel verbosity
    ) noexcept
{
    area->Verbosity = verbosity;
}

/// <summary>
/// Get the trace verbosity level of an trace area.
/// </summary>
/// <param name="area">The trace area.</param>
/// <returns>The verbosity level of the area.</returns>
/// <remarks>This should be accessed through macros, such as HC_TRACE_GET_VERBOSITY, rather than called directly.</remarks>
EXTERN_C inline
HCTraceLevel STDAPIVCALLTYPE HCTraceImplGetAreaVerbosity(struct HCTraceImplArea* area) noexcept
{
    return area->Verbosity;
}

/// <summary>
/// Send a trace message.
/// </summary>
/// <param name="area">The trace area.</param>
/// <param name="level">The trace level.</param>
/// <param name="format">The message format and arguments.</param>
/// <returns></returns>
/// <remarks>This should be accessed through macros, such as HC_TRACE_MESSAGE, rather than called directly.</remarks>
STDAPI_(void) HCTraceImplMessage(
    struct HCTraceImplArea const* area,
    HCTraceLevel level,
    _Printf_format_string_ char const* format,
    ...
) noexcept;

STDAPI_(uint64_t) HCTraceImplScopeId() noexcept;

}

#if defined(__cplusplus)
class HCTraceImplScopeHelper
{
public:
    HCTraceImplScopeHelper(HCTraceImplArea const* area, HCTraceLevel level, char const* scope) noexcept;
    ~HCTraceImplScopeHelper() noexcept;

private:
    HCTraceImplArea const* m_area;
    HCTraceLevel const m_level;
    char const* const m_scope;
    unsigned long long const m_id;
};

/// <summary>
/// HCTraceImplScopeHelper constructor. This should be accessed through macros, such as HC_TRACE_SCOPE, 
/// rather than called directly.
/// </summary>
/// <param name="area">The trace area.</param>
/// <param name="level">The trace level.</param>
/// <param name="scope">The trace scope.</param>
/// <returns></returns>
inline
HCTraceImplScopeHelper::HCTraceImplScopeHelper(
    HCTraceImplArea const* area,
    HCTraceLevel level, char const* scope
) noexcept
    : m_area{ area }, m_level{ level }, m_scope{ scope }, m_id{ HCTraceImplScopeId() }
{
    HCTraceImplMessage(m_area, m_level, ">>> %s (%016llX)", m_scope, m_id);
}

/// <summary>
/// HCTraceImplScopeHelper destructor.
/// </summary>
/// <returns></returns>
inline
HCTraceImplScopeHelper::~HCTraceImplScopeHelper() noexcept
{
    HCTraceImplMessage(m_area, m_level, "<<< %s (%016llX)", m_scope, m_id);
}
#endif // defined(__cplusplus)
