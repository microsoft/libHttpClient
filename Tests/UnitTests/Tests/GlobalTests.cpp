// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "singleton.h"


static bool g_gotCall = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(GlobalTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(GlobalTests);

    //void FormatFn(WCHAR* format, ...)
    //{
    //    va_list args;
    //    va_start(args, format);

    //    WCHAR sz[256];
    //    StringCchVPrintfW(sz, 256, format, args);
    //    va_end(args);

    //    size_t dest = 256;
    //    StringVPrintfWorkerW(sz, 256, &dest, format, args);
    //}

    DEFINE_TEST_CASE(TestGlobalFns)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGlobalFns);

        PCSTR_T ver;
        HCGlobalGetLibVersion(&ver);
        VERIFY_ARE_EQUAL_STR(_T("1.0.0.0"), ver);
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
