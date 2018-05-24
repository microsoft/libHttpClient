// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../Common/Win/utils_win.h"

using namespace xbox::httpclient;
static bool g_gotCall = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(GlobalTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(GlobalTests);

    DEFINE_TEST_CASE(TestFns)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestFns);

        PCSTR ver;
        HCGetLibVersion(&ver);
        VERIFY_ARE_EQUAL_STR("1.0.0.0", ver);

#pragma warning(disable: 4800)
        http_internal_wstring utf16 = utf16_from_utf8("test");
        VERIFY_ARE_EQUAL_STR(L"test", utf16.c_str());
        http_internal_string utf8 = utf8_from_utf16(L"test");
        VERIFY_ARE_EQUAL_STR("test", utf8.c_str());
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
