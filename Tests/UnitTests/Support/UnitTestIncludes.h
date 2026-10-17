// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

// Test framework backends:
//   USING_TAEF        - TAEF (Build/libHttpClient.UnitTest.TAEF)
//   UNITTEST_TE       - VS CppUnitTest / TE (Build/libHttpClient.UnitTest.TE)
//   UNITTEST_PORTABLE - in-house header-only runner (Support/Portable), used by the CMake builds.
//                       Also the default when neither of the Windows frameworks is selected.
#if !defined(USING_TAEF) && !defined(UNITTEST_TE) && !defined(UNITTEST_PORTABLE)
#define UNITTEST_PORTABLE 1
#endif

#ifdef USING_TAEF
#include "TAEF/UnitTestIncludes_TAEF.h"
#elif defined(UNITTEST_PORTABLE)
#include "Portable/UnitTestIncludes_Portable.h"
#include "DefineTestMacros.h"
#else
#include "TE/UnitTestIncludes_TE.h"
#include "DefineTestMacros.h"
#endif
#include "TestEnvironment.h"
