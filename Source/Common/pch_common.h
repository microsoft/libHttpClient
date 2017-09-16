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
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX

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
#include <codecvt>
#include <iomanip>

#if HC_UWP_API
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
#define HC_ASSERT(x) assert(x);
#else
#define HC_ASSERT(x) if(!(x)) throw std::invalid_argument("");
#endif

#if _MSC_VER <= 1800
typedef std::chrono::system_clock chrono_clock_t;
#else
typedef std::chrono::steady_clock chrono_clock_t;
#endif

#define NAMESPACE_XBOX_HTTP_CLIENT_BEGIN                     namespace xbox { namespace httpclient {
#define NAMESPACE_XBOX_HTTP_CLIENT_END                       }}
#define NAMESPACE_XBOX_HTTP_CLIENT_LOG_BEGIN                 namespace xbox { namespace httpclient { namespace log {
#define NAMESPACE_XBOX_HTTP_CLIENT_LOG_END                   }}}
#define NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN                namespace xbox { namespace httpclienttest {
#define NAMESPACE_XBOX_HTTP_CLIENT_TEST_END                  }}

#if !HC_UNITTEST_API
#define ENABLE_LOGS 1
#define ENABLE_ASSERTS 1
#endif

typedef int32_t function_context;
#include "httpClient/httpClient.h"
#include "../global/mem.h"
#include "win/utils_win.h"
#include "utils.h"
#include "../task/task_impl.h"
#include "../global/global.h"
#include "trace_internal.h"

HC_DECLARE_TRACE_AREA(HTTPCLIENT);
