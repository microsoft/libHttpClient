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

    typedef HRESULT hresult_t;

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
    typedef int32_t hresult_t;

    #define CALLBACK

    #define SEVERITY_SUCCESS    0
    #define SEVERITY_ERROR      1

    #define SUCCEEDED(hr)           (((hresult_t)(hr)) >= 0)
    #define FAILED(hr)              (((hresult_t)(hr)) < 0)

    #define HRESULT_CODE(hr)        ((hr) & 0xFFFF)
    #define SCODE_CODE(sc)          ((sc) & 0xFFFF)

    #define HRESULT_FACILITY(hr)    (((hr) >> 16) & 0x1fff)
    #define SCODE_FACILITY(sc)      (((sc) >> 16) & 0x1fff)

    #define HRESULT_SEVERITY(hr)    (((hr) >> 31) & 0x1)
    #define SCODE_SEVERITY(sc)      (((sc) >> 31) & 0x1)

    #define MAKE_HRESULT(sev,fac,code) \
        ((hresult_t) (((unsigned long)(sev)<<31) | ((unsigned long)(fac)<<16) | ((unsigned long)(code))) )
    #define MAKE_SCODE(sev,fac,code) \
        ((SCODE) (((unsigned long)(sev)<<31) | ((unsigned long)(fac)<<16) | ((unsigned long)(code))) )

    #define _HRESULT_TYPEDEF_(_sc) ((hresult_t)_sc)

    #define S_OK                             ((hresult_t)0L)
    #define E_NOTIMPL                        _HRESULT_TYPEDEF_(0x80004001L)
    #define E_OUTOFMEMORY                    _HRESULT_TYPEDEF_(0x8007000EL)
    #define E_INVALIDARG                     _HRESULT_TYPEDEF_(0x80070057L)
    #define E_FAIL                           _HRESULT_TYPEDEF_(0x80004005L)
    #define E_ACCESSDENIED                   _HRESULT_TYPEDEF_(0x80070005L)
    #define E_ABORT                          _HRESULT_TYPEDEF_(0x80000007L)

    #ifdef _In_
    #undef _In_
    #endif
    #define _In_

    #ifdef _Ret_maybenull_
    #undef _Ret_maybenull_
    #endif
    #define _Ret_maybenull_

    #ifdef _Post_writable_byte_size_
    #undef _Post_writable_byte_size_
    #endif
    #define _Post_writable_byte_size_(X)

#endif

#ifdef __cplusplus
    #define HC_NOEXCEPT noexcept
#else
    #define HC_NOEXCEPT
#endif

#define HC_CALLING_CONV __cdecl
#define HCAPI_(t) t HC_CALLING_CONV
#define HCAPI HCAPI_(hresult_t) 

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
typedef enum HCWebsocketCloseStatus
{
    HCWebsocketCloseStatus_Normal = 1000,
    HCWebsocketCloseStatus_GoingAway = 1001,
    HCWebsocketCloseStatus_ProtocolError = 1002,
    HCWebsocketCloseStatus_Unsupported = 1003,
    HCWebsocketCloseStatus_AbnormalClose = 1006,
    HCWebsocketCloseStatus_InconsistentDatatype = 1007,
    HCWebsocketCloseStatus_PolicyViolation = 1008,
    HCWebsocketCloseStatus_TooLarge = 1009,
    HCWebsocketCloseStatus_NegotiateError = 1010,
    HCWebsocketCloseStatus_ServerTerminate = 1011,
    HCWebsocketCloseStatus_UnknownError = 4000
} HCWebsocketCloseStatus;

