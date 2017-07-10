// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#pragma warning(disable: 4503) // C4503: decorated name length exceeded, name was truncated  
#pragma warning(disable: 4242) 

#ifdef _WIN32
// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.
#include <SDKDDKVer.h>

// Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#endif
#include <windows.h>
//#include <winapifamily.h>
#else
#define __STDC_LIMIT_MACROS
#include <stdint.h>

#include <boost/uuid/uuid.hpp>
#endif

// STL includes
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if UWP_API
#include <collection.h>
#endif

#ifndef _WIN32
#define UNREFERENCED_PARAMETER(args)
#endif

#ifndef max
#define max(x,y) std::max(x,y)
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) sizeof(x) / sizeof(x[0])
#endif

#ifndef UNIT_TEST_SERVICES
#define http_ASSERT(x) assert(x);
#else
#define http_ASSERT(x) if(!(x)) throw std::invalid_argument("");
#endif

#ifdef _WIN32
typedef wchar_t char_t;
#else
typedef char char_t;
#endif

#if _MSC_VER <= 1800
typedef std::chrono::system_clock chrono_clock_t;
#else
typedef std::chrono::steady_clock chrono_clock_t;
#endif

#define NAMESPACE_XBOX_LIBHCBEGIN                     namespace xbox { namespace livehttpclient {
#define NAMESPACE_XBOX_LIBHCEND                       }}


typedef int32_t function_context;
#include "httpClient/types.h"

