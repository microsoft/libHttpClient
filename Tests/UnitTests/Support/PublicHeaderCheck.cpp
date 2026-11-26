// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Compile-only check that the public headers are clean in a consumer translation unit on
// non-MSVC compilers. Every consumer TU includes them, so a diagnostic here breaks builds that
// use -Werror. Built by hc_add_unit_tests (Build/libHttpClient.CMake/UnitTests.cmake) for the
// native platform and for HC_PLATFORM_GENERIC, which stands in for console platforms.

#include <httpClient/config.h>
#include <httpClient/pal.h>
#include <httpClient/async.h>
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>
#include <httpClient/mock.h>
#include <httpClient/trace.h>
#include <XAsync.h>
#include <XAsyncProvider.h>
#include <XTaskQueue.h>
