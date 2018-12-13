// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "UnitTestHelpers.h"
#include "CppUnitTest.h"
#include <strsafe.h>

#define TEST_CLASS_AREA L"libHttpClient"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#define DEFINE_TEST_CASE(TestCaseMethodName)  \
    BEGIN_TEST_METHOD_ATTRIBUTE(TestCaseMethodName) \
        TEST_OWNER(TEST_CLASS_OWNER) \
        TEST_METHOD_ATTRIBUTE(L"Area", TEST_CLASS_AREA) \
    END_TEST_METHOD_ATTRIBUTE() \
    TEST_METHOD(TestCaseMethodName)

#define DEFINE_TEST_CASE_PROPERTIES_TE() ;

#define DEFINE_TEST_CASE_OWNER2(TestCaseMethodName)  \
    BEGIN_TEST_METHOD_ATTRIBUTE(TestCaseMethodName) \
        TEST_OWNER(TEST_CLASS_OWNER_2) \
        TEST_METHOD_ATTRIBUTE(L"Area", TEST_CLASS_AREA) \
    END_TEST_METHOD_ATTRIBUTE() \
    TEST_METHOD(TestCaseMethodName)

#define DEFINE_TEST_CLASS_SETUP() \
    TEST_CLASS_SETUP(TestClassSetup) { UnitTestBase::StartResponseLogging(); return true; } \
    TEST_CLASS_CLEANUP(TestClassCleanup) { UnitTestBase::RemoveResponseLogging(); return true; }  \

#define DEFINE_MOCK_FACTORY() \
    TEST_METHOD_SETUP(SetupFactory) \
    {  \
        return SetupFactoryHelper(); \
    }\
    TEST_METHOD_CLEANUP(CleanupTest) \
    { \
        return true; \
    }

#define DEFINE_MOCK_STATE_FACTORY() \
    TEST_METHOD_SETUP(SetupStateFactory) \
    {  \
        return SetupStateFactoryHelper(); \
    }\
    TEST_METHOD_CLEANUP(CleanupTest) \
    { \
        return true; \
    }

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

class AssertHelper
{
public:
    static void AreEqual(const char* expected, const char* actual)
    {
        Assert::AreEqual(expected, actual, false, nullptr, LINE_INFO());
    }

    static void AreEqual(const wchar_t* expected, const wchar_t* actual)
    {
        Assert::AreEqual(expected, actual, false, nullptr, LINE_INFO());
    }

    static void AreEqual(std::wstring expected, const wchar_t* actual)
    {
        Assert::AreEqual(expected.c_str(), actual);
    }

    static void AreEqual(const wchar_t* expected, std::wstring actual)
    {
        Assert::AreEqual(expected, actual.c_str());
    }

    static void AreEqual(std::wstring expected, std::wstring actual)
    {
        Assert::AreEqual(expected.c_str(), actual.c_str());
    }

    static void AreEqual(Platform::String^ expected, Platform::String^ actual)
    {
        Assert::AreEqual(expected->Data(), actual->Data());
    }

    static void AreEqual(bool expected, bool actual)
    {
        Assert::AreEqual(expected, actual);
    }

    static void AreEqual(double expected, double actual)
    {
        Assert::AreEqual(expected, actual);
    }

    static void AreEqual(std::chrono::duration<std::chrono::system_clock::rep, std::chrono::system_clock::period> expected, std::chrono::duration<std::chrono::system_clock::rep, std::chrono::system_clock::period> actual)
    {
        Assert::IsTrue(expected == actual);
    }

    static void AreEqual(HRESULT expected, HRESULT actual)
    {
        Assert::AreEqual((int)expected, (int)actual);
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END

#define VERIFY_SUCCEEDED(x) \
    xbox::httpclienttest::AssertHelper::AreEqual(S_OK, (HRESULT)x)

#define VERIFY_FAIL() \
    Assert::Fail()

#define VERIFY_ARE_EQUAL_UINT(expected, actual) \
    Assert::IsTrue(static_cast<uint64_t>(expected) == static_cast<uint64_t>(actual))

#define VERIFY_ARE_EQUAL_INT(expected, actual) \
    Assert::IsTrue(static_cast<int64_t>(expected) == static_cast<int64_t>(actual))

#define VERIFY_ARE_EQUAL(expected,actual) \
    xbox::httpclienttest::AssertHelper::AreEqual((double)expected,(double)actual)

#define VERIFY_ARE_EQUAL_STR(expected,actual) \
    xbox::httpclienttest::AssertHelper::AreEqual(expected,actual)

#define VERIFY_ARE_EQUAL_STR_IGNORE_CASE(expected,actual) \
    Assert::AreEqual(expected, actual, true)

#define VERIFY_IS_TRUE(x) \
    Assert::IsTrue(x, L#x)

#define VERIFY_IS_FALSE(x) \
    Assert::IsFalse(x, L#x)

#define VERIFY_IS_NULL(x) \
    Assert::IsTrue(x == nullptr, L#x)

#define VERIFY_IS_NOT_NULL(x) \
    Assert::IsTrue(x != nullptr, L#x)

#define VERIFY_IS_LESS_THAN(expectedLess, expectedGreater) \
    Assert::IsTrue(expectedLess < expectedGreater)

#define VERIFY_IS_GREATER_THAN_OR_EQUAL(expectedGreater, expectedLess) \
    Assert::IsTrue(expectedGreater >= expectedLess)

#define VERIFY_ARE_EQUAL_TIMESPAN_TO_SECONDS(__timespan, __seconds) VERIFY_ARE_EQUAL(Microsoft::Xbox::Services::System::timeSpanTicks(__timespan.Duration), std::chrono::seconds(__seconds))
#define VERIFY_ARE_EQUAL_TIMESPAN_TO_MILLISECONDS(__timespan, __seconds) VERIFY_ARE_EQUAL(Microsoft::Xbox::Services::System::timeSpanTicks(__timespan.Duration), std::chrono::milliseconds(__seconds))

#define VERIFY_THROWS_CX(__operation, __exception)                                                                                                                          \
{                                                                                                                                                                           \
    bool __exceptionHit = false;                                                                                                                                            \
    try                                                                                                                                                                     \
    {                                                                                                                                                                       \
        __operation;                                                                                                                                                        \
    }                                                                                                                                                                       \
    catch(__exception^ __e)                                                                                                                                                 \
    {                                                                                                                                                                       \
        Logger::WriteMessage( FormatString( L"Verify: Expected Exception Thrown( %s )", L#__exception ).c_str() );                                                          \
        __exceptionHit = true;                                                                                                                                              \
    }                                                                                                                                                                       \
    catch(...)                                                                                                                                                              \
    {                                                                                                                                                                       \
    }                                                                                                                                                                       \
    if(!__exceptionHit)                                                                                                                                                     \
    {                                                                                                                                                                       \
        Logger::WriteMessage( FormatString( L"Error: Expected Exception Not Thrown ( %s )", L#__exception ).c_str() );                                                      \
        Assert::IsTrue(__exceptionHit);                                                                                                                                     \
    }                                                                                                                                                                       \
}

#define VERIFY_THROWS_HR_CX(__operation, __hr)                                                                                                                                  \
{                                                                                                                                                                               \
    bool __exceptionHit = false;                                                                                                                                                \
    try                                                                                                                                                                         \
    {                                                                                                                                                                           \
        __operation;                                                                                                                                                            \
    }                                                                                                                                                                           \
    catch(Platform::Exception^ __e)                                                                                                                                             \
    {                                                                                                                                                                           \
        if(__e->HResult == (__hr))                                                                                                                                              \
        {                                                                                                                                                                       \
            Logger::WriteMessage( FormatString( L"Verify: Expected Exception Thrown ( hr == %s )", L#__hr ).c_str() );                                                          \
            __exceptionHit = true;                                                                                                                                              \
        }                                                                                                                                                                       \
    }                                                                                                                                                                           \
    catch(...)                                                                                                                                                                  \
    {                                                                                                                                                                           \
    }                                                                                                                                                                           \
    if(!__exceptionHit)                                                                                                                                                         \
    {                                                                                                                                                                           \
        Logger::WriteMessage( FormatString( L"Error: Expected Exception Not Thrown ( hr == %s )", L#__hr ).c_str() );                                                           \
        Assert::IsTrue(__exceptionHit);                                                                                                                                         \
    }                                                                                                                                                                           \
}


#define VERIFY_NO_THROW(__operation, ...)                                                                                          \
{                                                                                                                                    \
    bool __exceptionHit = false;                                                                                                     \
    try                                                                                                                              \
    {                                                                                                                                \
        __operation;                                                                                                                 \
    }                                                                                                                                \
    catch(...)                                                                                                                       \
    {                                                                                                                                \
        __exceptionHit = true;                                                                                                       \
    }                                                                                                                                \
                                                                                                                                     \
    if (__exceptionHit)                                                                                                              \
    {                                                                                                                                \
    }                                                                                                                                \
}        

//using namespace Platform::Collections;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;

#define LOG_COMMENT(x, ...)

