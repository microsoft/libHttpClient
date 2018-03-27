// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#pragma warning(disable: 4062)
#pragma warning(disable: 4702)
#include <stdint.h>

#ifdef _WIN32
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

#endif //#ifdef _WIN32

#if HC_UNITTEST_API
#undef HC_UWP_API
#define HC_UWP_API 1
#endif 

#ifndef _WIN32
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

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint32_t hc_memory_type;
typedef struct HC_WEBSOCKET* hc_websocket_handle;
typedef struct HC_CALL* hc_call_handle;
typedef struct HC_CALL* hc_mock_call_handle;

// Error codes from https://www.iana.org/assignments/websocket/websocket.xml#close-code-number
typedef enum HcWebsocketCloseStatus
{
    HcWebsocketCloseNormal = 1000,
    HcWebsocketCloseGoingAway = 1001,
    HcWebsocketCloseProtocolError = 1002,
    HcWebsocketCloseUnsupported = 1003, 
    HcWebsocketCloseAbnormalClose = 1006,
    HcWebsocketCloseInconsistentDatatype = 1007,
    HcWebsocketClosePolicyViolation = 1008,
    HcWebsocketCloseTooLarge = 1009,
    HcWebsocketCloseNegotiateError = 1010,
    HcWebsocketCloseServerTerminate = 1011,
    HcWebsocketCloseUnknownError = 4000
} HcWebsocketCloseStatus;


#ifdef __cplusplus
#define HC_NOEXCEPT noexcept
#else
#define HC_NOEXCEPT
#endif


#if defined(__cplusplus)
} // end extern "C"
#endif // defined(__cplusplus)
