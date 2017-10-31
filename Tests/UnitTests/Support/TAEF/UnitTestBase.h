// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "WexTestClass.h"

using namespace WEX::Logging;
using namespace WEX::TestExecution;
using namespace WEX::Common;
using namespace Windows::Foundation::Collections;


#define DATETIME_STRING_LENGTH_TO_SECOND 19
#define TICKS_PER_SECOND 10000000i64
typedef std::chrono::duration<long long, std::ratio<1, 10000000>> ticks;

////////////////////////////////////////////////////////////////////////////////
//
// TestBase class
//
////////////////////////////////////////////////////////////////////////////////
class UnitTestBase
{
public:
    /// <summary>
    /// Starts fiddler like response logging
    /// </summary>
    void StartResponseLogging();

    /// <summary>
    /// Stops response logging
    /// </summary>
    void RemoveResponseLogging();

    bool SetupFactoryHelper()
    {
        //if (m_mockXboxSystemFactory == nullptr)
        //{
        //    m_mockXboxSystemFactory = std::make_shared<MockXboxSystemFactory>();

        //    xbox_system_factory::set_factory(m_mockXboxSystemFactory);
        //}

        //m_mockXboxSystemFactory->reinit();
        return true;
    }

protected:
    //std::shared_ptr<xbox::services::system::MockXboxSystemFactory> m_mockXboxSystemFactory;
};

class UnitTestBaseProperties
{
public:
    UnitTestBaseProperties() {}
    UnitTestBaseProperties(LPCWSTR strMsg);

    ~UnitTestBaseProperties();

private:
    LPCWSTR m_strMsg;
};

std::wstring FormatString(LPCWSTR strMsg, ...);
void LogFormatString(LPCWSTR strMsg, ...);

#define VERIFY_ARE_EQUAL_STRING_IGNORE_CASE(__str1, __str2) COMPARE_STR_IGNORE_CASE_HELPER(__str1, __str2, PRIVATE_VERIFY_ERROR_INFO)

inline void COMPARE_STR_IGNORE_CASE_HELPER(LPCWSTR pwsz1, LPCWSTR pwsz2, const WEX::TestExecution::ErrorInfo& errorInfo)
{
    WEX::Logging::Log::Comment( FormatString(L"Verify: AreEqualIgnoreCase(%s, %s)", pwsz1, pwsz2).c_str() );
    if( _wcsicmp(pwsz1, pwsz2) != 0 )
    {
        WEX::Logging::Log::Error( FormatString( L"EXPECTED: \"%s\"", pwsz1 ).c_str());
        WEX::Logging::Log::Error( FormatString( L"ACTUAL: \"%s\"", pwsz2 ).c_str());
        WEX::TestExecution::Private::MacroVerify::IsTrue(false, L"false", errorInfo, nullptr);
    }
}

#define VERIFY_ARE_EQUAL_STR(__expected, __actual, ...) VERIFY_ARE_EQUAL_STR_HELPER((__expected), (__actual), (L#__actual), PRIVATE_VERIFY_ERROR_INFO)

inline void VERIFY_ARE_EQUAL_STR_HELPER(std::wstring expected, std::wstring actual, const wchar_t* pszParamName, const WEX::TestExecution::ErrorInfo& errorInfo)
{
    if (expected != actual)
    {
        WEX::Logging::Log::Error(FormatString(L"EXPECTED: %s = \"%s\"", pszParamName, expected.c_str()).c_str());
        WEX::Logging::Log::Error(FormatString(L"ACTUAL: %s = \"%s\"", pszParamName, actual.c_str()).c_str());
        WEX::TestExecution::Private::MacroVerify::IsTrue(false, L"false", errorInfo, nullptr);
    }
    else
    {
        WEX::Logging::Log::Comment(FormatString(L"Verify: AreEqual(%s,\"%s\")", pszParamName, expected.c_str()).c_str());
    }
}

inline void VERIFY_ARE_EQUAL_STR_HELPER(std::string expected, std::string actual, const wchar_t* pszParamName, const WEX::TestExecution::ErrorInfo& errorInfo)
{
    if (expected != actual)
    {
        WEX::Logging::Log::Error(FormatString(L"EXPECTED: %s = \"%S\"", pszParamName, expected.c_str()).c_str());
        WEX::Logging::Log::Error(FormatString(L"ACTUAL: %s = \"%S\"", pszParamName, actual.c_str()).c_str());
        WEX::TestExecution::Private::MacroVerify::IsTrue(false, L"false", errorInfo, nullptr);
    }
    else
    {
        WEX::Logging::Log::Comment(FormatString(L"Verify: AreEqual(%S,\"%S\")", pszParamName, expected.c_str()).c_str());
    }
}

#define VERIFY_THROWS_HR(__operation, __hr)                                                                                                                                     \
{                                                                                                                                                                               \
    bool __exceptionHit = false;                                                                                                                                                \
    try                                                                                                                                                                         \
    {                                                                                                                                                                           \
        __operation;                                                                                                                                                            \
    }                                                                                                                                                                           \
    catch(...)                                                                                                                                                                  \
    {                                                                                                                                                                           \
        HRESULT hrGot = Utils::ConvertExceptionToHRESULT();                                                                                                                     \
        if(hrGot == __hr)                                                                                                                                                       \
        {                                                                                                                                                                       \
            WEX::Logging::Log::Comment( FormatString( L"Verify: Expected Exception Thrown ( hr == %s )", L#__hr ).c_str() );                                                    \
            __exceptionHit = true;                                                                                                                                              \
        }                                                                                                                                                                       \
    }                                                                                                                                                                           \
    if(!__exceptionHit)                                                                                                                                                         \
    {                                                                                                                                                                           \
        WEX::Logging::Log::Comment( FormatString( L"Error: Expected Exception Not Thrown ( hr == %s )", L#__hr ).c_str() );                                                     \
        (bool)WEX::TestExecution::Private::MacroVerify::Fail(PRIVATE_VERIFY_ERROR_INFO, FormatString( L"Expected Exception Not Thrown ( hr == %s )", L#__hr ).c_str());         \
    }                                                                                                                                                                           \
}
