// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"

#define VERIFY_EXCEPTION_TO_HR(x,hrVerify) \
        try \
        { \
            throw x; \
        } \
        catch (...) \
        { \
            HRESULT hr = utils::convert_exception_to_hresult(); \
            VERIFY_ARE_EQUAL(hr, hrVerify); \
        }

class HttpTests : public UnitTestBase
{
public:
    TEST_CLASS(HttpTests);
    DEFINE_TEST_CLASS_SETUP();

    TEST_METHOD(TestMem)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMem);
    }

    TEST_METHOD(TestGlobal)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGlobal);
    }

    TEST_METHOD(TestThread)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestThread);
    }

    TEST_METHOD(TestSettings)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSettings);
    }

    TEST_METHOD(TestCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCall);
    }

    TEST_METHOD(TestRequest)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequest);
    }

    TEST_METHOD(TestResponse)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponse);
    }
};

