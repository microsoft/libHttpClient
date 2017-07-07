// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "singleton.h"

static void HC_CALLING_CONV PerformCallback(
    _In_ HC_CALL_HANDLE call
    )
{
}


class MockTests : public UnitTestBase
{
public:
    TEST_CLASS(MockTests);
    DEFINE_TEST_CLASS_SETUP();

    TEST_METHOD(ExampleMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleMock);
        HCGlobalInitialize();
        //HCGlobalSetHttpCallPerformCallback(&PerformCallback);
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"4");
        HCHttpCallRequestSetRetryAllowed(call, true);
        //HCHttpCallPerform(call);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }
};

