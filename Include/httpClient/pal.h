// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#pragma warning(disable: 4062) // enumerator 'identifier' in switch of enum 'enumeration' is not handled
#pragma warning(disable: 4702) // unreachable code

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <httpClient/config.h>

#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_UWA || HC_PLATFORM == HC_PLATFORM_XDK

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#ifndef HC_XDK_API
#define HC_XDK_API (HC_PLATFORM == HC_PLATFORM_XDK)
#endif

#ifndef HC_UWP_API
#define HC_UWP_API (HC_PLATFORM == HC_PLATFORM_UWA)
#endif

#if HC_UNITTEST_API
#undef HC_UWP_API
#define HC_UWP_API 1
#endif 

#define _HRESULTYPEDEF_(_sc) ((HRESULT)_sc)

// Windows defines these as an inline function so they cannot be
// used in a switch statement. (It would work if we required c++17 support)
#undef E_TIME_CRITICAL_THREAD
#undef E_NOT_SUFFICIENT_BUFFER

#ifndef E_TIME_CRITICAL_THREAD
#ifndef ERROR_TIME_CRITICAL_THREAD
#define ERROR_TIME_CRITICAL_THREAD 0x1A0
#endif
#define E_TIME_CRITICAL_THREAD           __HRESULT_FROM_WIN32(ERROR_TIME_CRITICAL_THREAD) // 0x800701A0
#endif

#ifndef E_NOT_SUPPORTED
#define E_NOT_SUPPORTED                  __HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) // 0x80070032
#endif

#ifndef E_NOT_SUFFICIENT_BUFFER
#define E_NOT_SUFFICIENT_BUFFER          __HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) // 0x8007007A
#endif

#else 
// not _WIN32
typedef int32_t HRESULT;

#define CALLBACK

#ifndef __cdecl
#define __cdecl 
#endif

#ifndef __forceinline 
#define __forceinline inline
#endif

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C extern
#endif
#endif

#ifndef HANDLE
typedef void* HANDLE;
#endif

#define SEVERITY_SUCCESS    0
#define SEVERITY_ERROR      1

#define SUCCEEDED(hr)           (((HRESULT)(hr)) >= 0)
#define FAILED(hr)              (((HRESULT)(hr)) < 0)

#define HRESULT_CODE(hr)        ((hr) & 0xFFFF)
#define SCODE_CODE(sc)          ((sc) & 0xFFFF)

#define HRESULT_FACILITY(hr)    (((hr) >> 16) & 0x1fff)
#define SCODE_FACILITY(sc)      (((sc) >> 16) & 0x1fff)

#define HRESULT_SEVERITY(hr)    (((hr) >> 31) & 0x1)
#define SCODE_SEVERITY(sc)      (((sc) >> 31) & 0x1)

#define MAKE_HRESULT(sev,fac,code) \
        ((HRESULT) (((unsigned long)(sev)<<31) | ((unsigned long)(fac)<<16) | ((unsigned long)(code))) )
#define MAKE_SCODE(sev,fac,code) \
        ((SCODE) (((unsigned long)(sev)<<31) | ((unsigned long)(fac)<<16) | ((unsigned long)(code))) )

#define _HRESULTYPEDEF_(_sc) ((HRESULT)_sc)

#define S_OK                             ((HRESULT)0L)
#define E_NOTIMPL                        _HRESULTYPEDEF_(0x80004001L)
#define E_OUTOFMEMORY                    _HRESULTYPEDEF_(0x8007000EL)
#define E_INVALIDARG                     _HRESULTYPEDEF_(0x80070057L)
#define E_ABORT                          _HRESULTYPEDEF_(0x80004004L)
#define E_FAIL                           _HRESULTYPEDEF_(0x80004005L)
#define E_ACCESSDENIED                   _HRESULTYPEDEF_(0x80070005L)
#define E_PENDING                        _HRESULTYPEDEF_(0x8000000AL)
#define E_UNEXPECTED                     _HRESULTYPEDEF_(0x8000FFFFL)
#define E_POINTER                        _HRESULTYPEDEF_(0x80004003L)
#define E_TIME_CRITICAL_THREAD           _HRESULTYPEDEF_(0x800701A0L)
#define E_NOT_SUPPORTED                  _HRESULTYPEDEF_(0x80070032L)
#define E_NOT_SUFFICIENT_BUFFER          _HRESULTYPEDEF_(0x8007007AL)

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY  *Flink;
    struct _LIST_ENTRY  *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

//
// Calculate the address of the base of the structure given its type, and an
// address of a field within the structure.
//

#define CONTAINING_RECORD(address, type, field) \
        ((type *)((char*)(address) - (uintptr_t)(&((type *)0)->field)))

#ifndef _Printf_format_string_
#define _Printf_format_string_ 
#endif

#ifndef _Post_invalid_
#define _Post_invalid_ 
#endif

#ifndef _In_
#define _In_
#endif

#ifndef _In_opt_
#define _In_opt_ 
#endif

#ifndef _In_z_
#define _In_z_ 
#endif

#ifndef _In_opt_z_
#define _In_opt_z_ 
#endif

#ifndef _In_reads_bytes_
#define _In_reads_bytes_(size) 
#endif

#ifndef _In_reads_
#define _In_reads_(size) 
#endif

#ifndef _In_reads_bytes_opt_
#define _In_reads_bytes_opt_(size) 
#endif

#ifndef _Inout_
#define _Inout_ 
#endif

#ifndef _Inout_updates_bytes_
#define _Inout_updates_bytes_(size)
#endif

#ifndef _Out_
#define _Out_ 
#endif

#ifndef _Out_range_
#define _Out_range_(x, y)  
#endif

#ifndef _Out_opt_
#define _Out_opt_ 
#endif

#ifndef _Out_writes_
#define _Out_writes_(bytes)
#endif

#ifndef _Out_writes_z_
#define _Out_writes_z_(bytes)
#endif

#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(bytes)
#endif

#ifndef _Out_writes_to_
#define _Out_writes_to_(bytes, buffer)
#endif

#ifndef _Out_writes_to_opt_
#define _Out_writes_to_opt_(buffersize, size)
#endif

#ifndef _Out_writes_bytes_opt_
#define _Out_writes_bytes_opt_(size)
#endif

#ifndef _Out_writes_bytes_to_opt_
#define _Out_writes_bytes_to_opt_(size, buffer)
#endif

#ifndef _Outptr_
#define _Outptr_ 
#endif

#ifndef _Outptr_result_bytebuffer_maybenull_
#define _Outptr_result_bytebuffer_maybenull_(size)
#endif

#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif

#ifndef _Post_writable_byte_size_
#define _Post_writable_byte_size_(X)
#endif

#ifndef _Field_z_
#define _Field_z_ 
#endif

#ifndef _Field_size_
#define _Field_size_(bytes) 
#endif

#ifndef _Field_size_bytes_
#define _Field_size_bytes_(bytes) 
#endif

#ifndef _Field_size_bytes_opt_
#define _Field_size_bytes_opt_(bytes) 
#endif

#ifndef __analysis_assume
#define __analysis_assume(condition)
#endif

#ifndef STDAPIVCALLTYPE
#define STDAPIVCALLTYPE         __cdecl
#endif

#ifndef STDAPI
#define STDAPI                  EXTERN_C HRESULT STDAPIVCALLTYPE
#endif

#ifndef STDAPI_
#define STDAPI_(type)           EXTERN_C type STDAPIVCALLTYPE
#endif

#ifndef _Null_terminated_
#define _Null_terminated_ 
#endif

#endif

#ifdef __cplusplus
#define HC_NOEXCEPT noexcept
#else
#define HC_NOEXCEPT
#endif

#define FACILITY_XBOX 2339
#define MAKE_E_HC(code)                 MAKE_HRESULT(1, FACILITY_XBOX, code)

#define E_HC_NOT_INITIALISED            MAKE_E_HC(0x5001)
#define E_HC_PERFORM_ALREADY_CALLED     MAKE_E_HC(0x5003)
#define E_HC_ALREADY_INITIALISED        MAKE_E_HC(0x5004)
#define E_HC_CONNECT_ALREADY_CALLED     MAKE_E_HC(0x5005)

typedef uint32_t hc_memory_type;
typedef struct HC_WEBSOCKET* hc_websocket_handle_t;
typedef struct HC_CALL* hc_call_handle_t;
typedef struct HC_CALL* hc_mock_call_handle;

// Error codes from https://www.iana.org/assignments/websocket/websocket.xml#close-code-number
typedef enum HCWebSocketCloseStatus
{
    HCWebSocketCloseStatus_Normal = 1000,
    HCWebSocketCloseStatus_GoingAway = 1001,
    HCWebSocketCloseStatus_ProtocolError = 1002,
    HCWebSocketCloseStatus_Unsupported = 1003,
    HCWebSocketCloseStatus_AbnormalClose = 1006,
    HCWebSocketCloseStatus_InconsistentDatatype = 1007,
    HCWebSocketCloseStatus_PolicyViolation = 1008,
    HCWebSocketCloseStatus_TooLarge = 1009,
    HCWebSocketCloseStatus_NegotiateError = 1010,
    HCWebSocketCloseStatus_ServerTerminate = 1011,
    HCWebSocketCloseStatus_UnknownError = 4000
} HCWebSocketCloseStatus;

