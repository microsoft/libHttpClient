// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

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

#else

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

typedef void* handle_t;
typedef _Null_terminated_ char* utf8_string;
typedef _Null_terminated_ const char* const_utf8_string;

