// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#pragma warning(disable: 4062)

#include <stdint.h>

#ifdef _WIN32
    #include <windows.h>

    #ifndef _WIN32_WINNT_WIN10
    #define _WIN32_WINNT_WIN10 0x0A00
    #endif

    #ifndef XDK_API
    #define XDK_API (WINAPI_FAMILY == WINAPI_FAMILY_TV_APP || WINAPI_FAMILY == WINAPI_FAMILY_TV_TITLE) 
    #endif

    #ifndef HC_UWP_API
    #define HC_UWP_API (WINAPI_FAMILY == WINAPI_FAMILY_APP && _WIN32_WINNT >= _WIN32_WINNT_WIN10)
    #endif

#endif //#ifdef _WIN32

#if HC_UNITTEST_API
#undef HC_UWP_API
#define HC_UWP_API 1
#endif 

#if HC_UWP_API || HC_WIN32_API || HC_XDK_API
#undef HC_USE_HANDLES
#define HC_USE_HANDLES 1
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

#if defined _WIN32
  #ifdef HC_API_NONE
    #define HC_API
    #if _MSC_VER >= 1900
        #define HC_API_DEPRECATED __declspec(deprecated)
    #else
        #define HC_API_DEPRECATED
    #endif
  #else
    #ifdef HC_API_EXPORT
      #define HC_API __declspec(dllexport)
      #define HC_API_DEPRECATED __declspec(dllexport, deprecated)
    #else
      #define HC_API __declspec(dllimport)
      #define HC_API_DEPRECATED __declspec(dllimport, deprecated)
    #endif
  #endif
#else
  #if defined _NOHC_API || __GNUC__ < 4
     #define HC_API
     #define HC_API_DEPRECATED __attribute__ ((deprecated))
  #else
    #define HC_API __attribute__ ((visibility ("default")))
    #define HC_API_DEPRECATED __attribute__ ((visibility ("default"), deprecated))
  #endif
#endif

#ifdef _WIN32
    #ifdef __cplusplus
        #pragma detect_mismatch("HC_CHAR_T_IS_WIDE", "1")
    #endif
    #define HC_CHAR_T_IS_WIDE 1
#else
    #ifdef __cplusplus
        #pragma detect_mismatch("HC_CHAR_T_IS_WIDE", "0")
    #endif
    #define HC_CHAR_T_IS_WIDE 0
#endif

#if HC_CHAR_T_IS_WIDE
    typedef wchar_t CHAR_T;
    typedef LPCWSTR PCSTR_T;
    #ifndef _T
        #define _T(x) L ## x
    #endif
#else
    typedef char CHAR_T;
    typedef const char* PCSTR_T;
    #ifndef _T
        #define _T(x) x
    #endif
#endif

#define HC_CALLING_CONV __cdecl
typedef uint32_t HC_MEMORY_TYPE;
typedef struct HC_CALL* HC_CALL_HANDLE;
typedef uint64_t HC_TASK_HANDLE;
