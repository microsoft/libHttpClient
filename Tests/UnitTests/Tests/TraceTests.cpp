// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

std::atomic<uint32_t> s_firstCallbackInvocations{ 0 };
std::atomic<uint32_t> s_secondCallbackInvocations{ 0 };

void CALLBACK FirstTraceCallback(
    _In_z_ const char*,
    _In_ HCTraceLevel,
    _In_ uint64_t,
    _In_ uint64_t,
    _In_z_ const char*
) noexcept
{
    ++s_firstCallbackInvocations;
}

void CALLBACK SecondTraceCallback(
    _In_z_ const char*,
    _In_ HCTraceLevel,
    _In_ uint64_t,
    _In_ uint64_t,
    _In_z_ const char*
) noexcept
{
    ++s_secondCallbackInvocations;
}

void EmitTrace() noexcept
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "TraceTests trace");
}

class TraceTestScope
{
public:
    TraceTestScope() noexcept
    {
        m_getTraceLevelResult = HCSettingsGetTraceLevel(&m_previousTraceLevel);
        HCTraceInit();
        m_setTraceLevelResult = HCSettingsSetTraceLevel(HCTraceLevel::Information);
        ResetCallbackInvocations();
    }

    ~TraceTestScope()
    {
        HCTraceCleanup();
        if (SUCCEEDED(m_getTraceLevelResult))
        {
            HCSettingsSetTraceLevel(m_previousTraceLevel);
        }
    }

    TraceTestScope(TraceTestScope const&) = delete;
    TraceTestScope& operator=(TraceTestScope const&) = delete;

    void VerifyInitialized() const
    {
        VERIFY_SUCCEEDED(m_getTraceLevelResult);
        VERIFY_SUCCEEDED(m_setTraceLevelResult);
    }

    static void ResetCallbackInvocations() noexcept
    {
        s_firstCallbackInvocations = 0;
        s_secondCallbackInvocations = 0;
    }

private:
    HCTraceLevel m_previousTraceLevel{ HCTraceLevel::Off };
    HRESULT m_getTraceLevelResult{ E_FAIL };
    HRESULT m_setTraceLevelResult{ E_FAIL };
};

}

DEFINE_TEST_CLASS(TraceTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(TraceTests);

    DEFINE_TEST_CASE(TestSetClientCallback)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSetClientCallback);
        TraceTestScope scope;
        scope.VerifyInitialized();

        VERIFY_IS_TRUE(HCTraceSetClientCallback(&FirstTraceCallback));
        EmitTrace();
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_firstCallbackInvocations.load());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(0), s_secondCallbackInvocations.load());

        TraceTestScope::ResetCallbackInvocations();
        VERIFY_IS_TRUE(HCTraceSetClientCallback(&FirstTraceCallback));
        EmitTrace();
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_firstCallbackInvocations.load());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(0), s_secondCallbackInvocations.load());

        TraceTestScope::ResetCallbackInvocations();
        VERIFY_IS_TRUE(HCTraceSetClientCallback(&SecondTraceCallback));
        EmitTrace();
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_firstCallbackInvocations.load());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_secondCallbackInvocations.load());
    }

    DEFINE_TEST_CASE(TestRemoveClientCallback)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRemoveClientCallback);
        TraceTestScope scope;
        scope.VerifyInitialized();

        VERIFY_IS_TRUE(HCTraceSetClientCallback(&FirstTraceCallback));
        VERIFY_IS_TRUE(HCTraceSetClientCallback(&SecondTraceCallback));

        HCTraceRemoveClientCallback(nullptr);
        EmitTrace();
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_firstCallbackInvocations.load());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_secondCallbackInvocations.load());

        TraceTestScope::ResetCallbackInvocations();
        HCTraceRemoveClientCallback(&SecondTraceCallback);
        EmitTrace();
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1), s_firstCallbackInvocations.load());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(0), s_secondCallbackInvocations.load());

        TraceTestScope::ResetCallbackInvocations();
        HCTraceRemoveClientCallback(&FirstTraceCallback);
        EmitTrace();
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(0), s_firstCallbackInvocations.load());
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(0), s_secondCallbackInvocations.load());
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
