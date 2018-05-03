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
#if HC_UWP_API
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#endif
#define NOMINMAX

#include <windows.h>
#else
#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include "pal_internal.h"
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

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) sizeof(x) / sizeof(x[0])
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
#define NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_BEGIN              namespace xbox { namespace httpclient { namespace detail {
#define NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_END                }}}
#define NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN                namespace xbox { namespace httpclienttest {
#define NAMESPACE_XBOX_HTTP_CLIENT_TEST_END                  }}

#if !HC_UNITTEST_API
#define ENABLE_LOGS 1
#endif

#ifndef DebugBreak
#define DebugBreak()
#endif

#define ASSERT(condition) assert(condition)

#include <httpClient/httpClient.h>
#include "../Global/mem.h"

#if HC_PLATFORM_IS_MICROSOFT
#include "Win/utils_win.h"
#endif

#include "utils.h"
#include "../Global/global.h"
#include "trace_internal.h"
#include "ResultMacros.h"
#include "EntryList.h"

HC_DECLARE_TRACE_AREA(HTTPCLIENT);
HC_DECLARE_TRACE_AREA(WEBSOCKET);

// Define TRACE for AsyncLib
#define ASYNC_LIB_TRACE(result, message)	        \
    HC_TRACE_ERROR_HR(HTTPCLIENT, result, message); \

#define CATCH_RETURN() CATCH_RETURN_IMPL(__FILE__, __LINE__)

#define CATCH_RETURN_IMPL(file, line) \
    catch (std::bad_alloc const& e) { return ::xbox::httpclient::detail::StdBadAllocToResult(e, file, line); } \
    catch (std::exception const& e) { return ::xbox::httpclient::detail::StdExceptionToResult(e, file, line); } \
    catch (...) { return ::xbox::httpclient::detail::UnknownExceptionToResult(file, line); }

#define CATCH_RETURN_WITH(errCode) CATCH_RETURN_IMPL_WITH(__FILE__, __LINE__, errCode)

#define CATCH_RETURN_IMPL_WITH(file, line, errCode) \
    catch (std::bad_alloc const& e) { ::xbox::httpclient::detail::StdBadAllocToResult(e, file, line); return errCode; } \
    catch (std::exception const& e) { ::xbox::httpclient::detail::StdExceptionToResult(e, file, line); return errCode; } \
    catch (...) { ::xbox::httpclient::detail::UnknownExceptionToResult(file, line); return errCode; }

#define RETURN_IF_PERFORM_CALLED(call) if (call->performCalled) return E_HC_PERFORM_ALREADY_CALLED;
#define RETURN_IF_WEBSOCKET_CONNECT_CALLED(socket) if (socket->connectCalled) return E_HC_CONNECT_ALREADY_CALLED;

NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_BEGIN

HRESULT StdBadAllocToResult(std::bad_alloc const& e, _In_z_ char const* file, uint32_t line);
HRESULT StdExceptionToResult(std::exception const& e, _In_z_ char const* file, uint32_t line);
HRESULT UnknownExceptionToResult(_In_z_ char const* file, uint32_t line);

NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_END
