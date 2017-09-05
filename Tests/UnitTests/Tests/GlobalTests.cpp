// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "singleton.h"

using namespace xbox::httpclient;
static bool g_gotCall = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(GlobalTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(GlobalTests);

    DEFINE_TEST_CASE(TestGlobalFns)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGlobalFns);

        PCSTR_T ver;
        HCGlobalGetLibVersion(&ver);
        VERIFY_ARE_EQUAL_STR(_T("1.0.0.0"), ver);

#pragma warning(disable: 4800)
        std::wstring test1 = to_wstring(L"test");
        VERIFY_ARE_EQUAL_STR(L"test", test1.c_str());
        std::wstring test2 = to_wstring("test");
        VERIFY_ARE_EQUAL_STR(L"test", test2.c_str());
        std::string test3 = to_utf8string(L"test");
        VERIFY_ARE_EQUAL_STR("test", test3.c_str());
        std::string test4 = to_utf8string("test");
        VERIFY_ARE_EQUAL_STR("test", test4.c_str());
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
