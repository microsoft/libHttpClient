// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#pragma warning(disable: 4062)
#pragma warning(disable: 4702)

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32

    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif

    #ifndef NOMINMAX
    #define NOMINMAX
    #endif

    #include <windows.h>

    #ifndef _WIN32_WINNT_WIN10
    #define _WIN32_WINNT_WIN10 0x0A00
    #endif

    #ifndef HC_XDK_API
    #define HC_XDK_API (WINAPI_FAMILY == WINAPI_FAMILY_TV_APP || WINAPI_FAMILY == WINAPI_FAMILY_TV_TITLE) 
    #endif

    #ifndef HC_UWP_API
    #define HC_UWP_API (WINAPI_FAMILY == WINAPI_FAMILY_APP && _WIN32_WINNT >= _WIN32_WINNT_WIN10)
    #endif

    #if HC_UNITTEST_API
        #undef HC_UWP_API
        #define HC_UWP_API 1
    #endif 

#else 

    // not _WIN32
    typedef int32_t HRESULT;

    #define CALLBACK

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
    #define E_FAIL                           _HRESULTYPEDEF_(0x80004005L)
    #define E_ACCESSDENIED                   _HRESULTYPEDEF_(0x80070005L)
    #define E_ABORT                          _HRESULTYPEDEF_(0x80000007L)

    #ifndef _In_
    #define _In_
    #endif

    #ifndef _Ret_maybenull_
    #define _Ret_maybenull_
    #endif

    #ifndef _Post_writable_byte_size_
    #define _Post_writable_byte_size_(X)
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

#endif

#ifdef __cplusplus
    #define HC_NOEXCEPT noexcept
#else
    #define HC_NOEXCEPT
#endif

#define FACILITY_XBOX 2339
#define MAKE_E_HC(code)                 MAKE_HRESULT(1, FACILITY_XBOX, code)

#define E_HC_BUFFER_TOO_SMALL           MAKE_E_HC(5000L)
#define E_HC_NOT_INITIALISED            MAKE_E_HC(5001L)
#define E_HC_FEATURE_NOT_PRESENT        MAKE_E_HC(5002L)
#define E_HC_PERFORM_ALREADY_CALLED     MAKE_E_HC(5003L)
#define E_HC_ALREADY_INITIALISED        MAKE_E_HC(5004L)
#define E_HC_CONNECT_ALREADY_CALLED     MAKE_E_HC(5005L)

typedef _Null_terminated_ char* utf8_string;
typedef _Null_terminated_ const char* const_utf8_string;

typedef uint32_t hc_memory_type;
typedef struct HC_WEBSOCKET* hc_websocket_handle;
typedef struct HC_CALL* hc_call_handle;
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

