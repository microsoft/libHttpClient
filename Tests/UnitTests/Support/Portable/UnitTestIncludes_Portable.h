// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Portable, header-only unit-test runner used to build Tests/UnitTests/Tests/*.cpp outside of
// Visual Studio's CppUnitTest (TE) and TAEF, e.g. on Linux and macOS through CMake/ctest.
//
// It provides the small, fixed macro set the tests use (TEST_CLASS / TEST_METHOD /
// TEST_METHOD_SETUP / TEST_METHOD_INITIALIZE / TEST_METHOD_CLEANUP and the VERIFY_* family). The DEFINE_TEST_* wrappers
// are mapped onto these in DefineTestMacros.h, exactly like the TE backend.
//
// Semantics follow the TE backend: every test method runs on a fresh instance of its class,
// TEST_METHOD_SETUP runs before and TEST_METHOD_CLEANUP after it (even when the test failed),
// and a failing VERIFY_* throws to abort the test. A failure is also recorded globally before
// throwing, so a VERIFY that fails on a worker thread (whose exception may be swallowed by the
// code under test) still fails the current test.
//
// The test executable's main() lives in UnitTestMain_Portable.cpp and calls RunAllTests().
// Usage: <exe> [--list] [--filter=<glob>[,<glob>...]] [<glob>...]
//   Globs ('*' and '?') match "Class::Method" or just "Method"; a leading '-' excludes.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if !HC_PLATFORM_IS_MICROSOFT
#include "Win32Shims_Portable.h"
#endif

namespace hcunittest
{

struct TestFailure : std::exception
{
    explicit TestFailure(std::string message) : m_message{ std::move(message) } {}
    const char* what() const noexcept override { return m_message.c_str(); }
    std::string m_message;
};

struct TestCaseInfo
{
    std::string className;
    std::string methodName;
    std::function<void()> run;
};

inline std::vector<TestCaseInfo>& Registry()
{
    static std::vector<TestCaseInfo> s_registry;
    return s_registry;
}

inline void RegisterTest(const char* className, const char* methodName, std::function<void()> run)
{
    Registry().push_back(TestCaseInfo{ className, methodName, std::move(run) });
}

// Failures recorded for the currently running test (may be written from any thread).
struct FailureLog
{
    std::mutex lock;
    std::vector<std::string> messages;
};

inline FailureLog& Failures()
{
    static FailureLog* s_log = new FailureLog{}; // intentionally leaked: detached threads may outlive statics
    return *s_log;
}

inline void RecordFailure(const std::string& message)
{
    std::lock_guard<std::mutex> guard{ Failures().lock };
    Failures().messages.push_back(message);
}

// True once the current test has recorded a failure (TestEnvironment uses it so a test's own
// failure stays the reported one instead of its follow-on leak report).
inline bool HasRecordedFailures()
{
    std::lock_guard<std::mutex> guard{ Failures().lock };
    return !Failures().messages.empty();
}

// Informational output for the current test (TestEnvironment's reset/leak notes).
inline void LogMessage(const std::string& message)
{
    std::printf("    [log] %s\n", message.c_str());
    std::fflush(stdout);
}

[[noreturn]] inline void Fail(const char* file, int line, const std::string& message)
{
    std::ostringstream text;
    text << file << "(" << line << "): " << message;
    RecordFailure(text.str());
    throw TestFailure{ text.str() };
}

// ---- value formatting / comparison -------------------------------------------------------

template<typename T, typename = void> struct IsStreamable : std::false_type {};
template<typename T> struct IsStreamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>> : std::true_type {};

template<typename T> struct IsAtomic : std::false_type {};
template<typename T> struct IsAtomic<std::atomic<T>> : std::true_type {};

// Enums compare/print as their underlying value, std::atomic as its current value.
template<typename T> auto Normalize(const T& v)
{
    if constexpr (IsAtomic<T>::value) { return Normalize(v.load()); }
    else if constexpr (std::is_enum_v<T>) { return static_cast<std::underlying_type_t<T>>(v); }
    else { return v; }
}

template<typename T> std::string Format(const T& value)
{
    auto v = Normalize(value);
    using V = decltype(v);
    std::ostringstream s;
    if constexpr (std::is_same_v<V, bool>) { s << (v ? "true" : "false"); }
    else if constexpr (std::is_integral_v<V>)
    {
        s << static_cast<std::conditional_t<std::is_signed_v<V>, long long, unsigned long long>>(v);
        s << " (0x" << std::hex << static_cast<unsigned long long>(static_cast<std::make_unsigned_t<V>>(v)) << ")";
    }
    else if constexpr (std::is_pointer_v<V>) { s << static_cast<const void*>(v); }
    else if constexpr (IsStreamable<V>::value) { s << v; }
    else { s << "<value>"; }
    return s.str();
}

template<typename A, typename B> bool ValuesEqual(const A& a, const B& b)
{
    auto x = Normalize(a);
    auto y = Normalize(b);
    using X = decltype(x);
    using Y = decltype(y);
    if constexpr (std::is_integral_v<X> && std::is_integral_v<Y> && std::is_signed_v<X> != std::is_signed_v<Y>)
    {
        // Mixed signedness: compare mathematically instead of via implicit conversion.
        if constexpr (std::is_signed_v<X>) { return x >= 0 && static_cast<unsigned long long>(x) == static_cast<unsigned long long>(y); }
        else { return y >= 0 && static_cast<unsigned long long>(x) == static_cast<unsigned long long>(y); }
    }
    else if constexpr (std::is_arithmetic_v<X> && std::is_arithmetic_v<Y>) { return x == y; }
    else { return a == b; }
}

template<typename A, typename B>
void VerifyAreEqual(const A& expected, const B& actual, bool wantEqual, const char* expectedText, const char* actualText, const char* file, int line)
{
    if (ValuesEqual(expected, actual) != wantEqual)
    {
        Fail(file, line, std::string(wantEqual ? "VERIFY_ARE_EQUAL(" : "VERIFY_ARE_NOT_EQUAL(") + expectedText + ", " + actualText +
            ") - expected: " + Format(expected) + ", actual: " + Format(actual));
    }
}

// Strings: accept char/wchar_t/char16_t/char32_t pointers and std::basic_string with any allocator.
template<typename C> std::u32string ToU32(const C* s)
{
    std::u32string out;
    for (; s != nullptr && *s; ++s) { out.push_back(static_cast<char32_t>(static_cast<std::make_unsigned_t<C>>(*s))); }
    return out;
}
template<typename C, typename Tr, typename Al> std::u32string ToU32(const std::basic_string<C, Tr, Al>& s) { return ToU32(s.c_str()); }

inline std::string ToPrintable(const std::u32string& s)
{
    std::string out;
    for (char32_t c : s) { if (c < 0x80) { out.push_back(static_cast<char>(c)); } else { out += "\\u{" + Format(static_cast<uint32_t>(c)) + "}"; } }
    return out;
}

inline char32_t FoldCase(char32_t c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

template<typename A, typename B>
void VerifyStringsEqual(const A& expected, const B& actual, bool ignoreCase, const char* expectedText, const char* actualText, const char* file, int line)
{
    std::u32string e = ToU32(expected);
    std::u32string a = ToU32(actual);
    bool equal = e.size() == a.size();
    for (size_t i = 0; equal && i < e.size(); ++i) { equal = ignoreCase ? FoldCase(e[i]) == FoldCase(a[i]) : e[i] == a[i]; }
    if (!equal)
    {
        Fail(file, line, std::string("VERIFY_ARE_EQUAL_STR(") + expectedText + ", " + actualText + ") - expected: \"" + ToPrintable(e) + "\", actual: \"" + ToPrintable(a) + "\"");
    }
}

inline void VerifyCondition(bool condition, const char* macro, const char* text, const char* file, int line)
{
    if (!condition) { Fail(file, line, std::string(macro) + "(" + text + ")"); }
}

// ---- test classes ------------------------------------------------------------------------

template<typename T, typename Name>
class TestClass
{
public:
    using HcThisClass = T;
    static const char* HcClassName() { return Name::Get(); }

    virtual ~TestClass() = default;
    virtual void HcMethodSetup() {}
    virtual void HcMethodCleanup() {}

    template<typename M> static void HcRun(M method)
    {
        T instance{};
        instance.HcMethodSetup();
        try { (instance.*method)(); }
        catch (...) { try { instance.HcMethodCleanup(); } catch (...) {} throw; }
        instance.HcMethodCleanup();
    }
};

// ---- runner ------------------------------------------------------------------------------

inline bool GlobMatch(const char* pattern, const char* text)
{
    if (*pattern == '\0') { return *text == '\0'; }
    if (*pattern == '*') { return GlobMatch(pattern + 1, text) || (*text != '\0' && GlobMatch(pattern, text + 1)); }
    return *text != '\0' && (*pattern == '?' || *pattern == *text) && GlobMatch(pattern + 1, text + 1);
}

inline bool IsSelected(const TestCaseInfo& test, const std::vector<std::string>& patterns)
{
    std::string fullName = test.className + "::" + test.methodName;
    bool anyInclude = false;
    bool included = false;
    for (const std::string& p : patterns)
    {
        bool exclude = !p.empty() && p[0] == '-';
        const char* glob = p.c_str() + (exclude ? 1 : 0);
        bool match = GlobMatch(glob, fullName.c_str()) || GlobMatch(glob, test.methodName.c_str());
        if (exclude && match) { return false; }
        if (!exclude) { anyInclude = true; included = included || match; }
    }
    return !anyInclude || included;
}

inline int RunAllTests(int argc, char** argv)
{
    std::vector<std::string> patterns;
    bool listOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--list") { listOnly = true; continue; }
        if (arg == "--help" || arg == "-h") { std::printf("usage: %s [--list] [--filter=<glob>[,<glob>...]] [<glob>...]\n", argv[0]); return 0; }
        if (arg.rfind("--filter=", 0) == 0) { arg = arg.substr(9); }
        std::stringstream parts{ arg };
        for (std::string part; std::getline(parts, part, ',');) { if (!part.empty()) { patterns.push_back(part); } }
    }

    std::vector<std::string> failedTests;
    size_t ran = 0;
    auto suiteStart = std::chrono::steady_clock::now();
    for (const TestCaseInfo& test : Registry())
    {
        if (!IsSelected(test, patterns)) { continue; }
        std::string name = test.className + "::" + test.methodName;
        if (listOnly) { std::printf("%s\n", name.c_str()); continue; }

        ++ran;
        std::printf("[ RUN      ] %s\n", name.c_str());
        std::fflush(stdout);
        { std::lock_guard<std::mutex> guard{ Failures().lock }; Failures().messages.clear(); }

        auto start = std::chrono::steady_clock::now();
        try { test.run(); }
        catch (const TestFailure&) {} // already recorded
        catch (const std::exception& e) { RecordFailure(std::string("unhandled exception: ") + e.what()); }
        catch (...) { RecordFailure("unhandled non-standard exception"); }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

        std::vector<std::string> messages;
        { std::lock_guard<std::mutex> guard{ Failures().lock }; messages.swap(Failures().messages); }
        for (const std::string& m : messages) { std::printf("    %s\n", m.c_str()); }
        std::printf("[ %s ] %s (%lld ms)\n", messages.empty() ? "    PASS" : "  FAILED", name.c_str(), static_cast<long long>(ms));
        std::fflush(stdout);
        if (!messages.empty()) { failedTests.push_back(name); }
    }

    if (listOnly) { return 0; }
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - suiteStart).count();
    std::printf("\n%zu tests ran (%lld ms): %zu passed, %zu failed\n", ran, static_cast<long long>(totalMs), ran - failedTests.size(), failedTests.size());
    for (const std::string& f : failedTests) { std::printf("  FAILED: %s\n", f.c_str()); }
    std::fflush(stdout); // sanitizers may _exit() from atexit handlers before stdio is flushed
    if (ran == 0) { std::printf("No tests matched the filter.\n"); return 1; }
    return failedTests.empty() ? 0 : 1;
}

} // namespace hcunittest

// ---- macros used by the tests (see also DefineTestMacros.h) -------------------------------

#define TEST_CLASS(className) \
    struct HcTestClassName_##className { static const char* Get() { return #className; } }; \
    class className : public ::hcunittest::TestClass<className, HcTestClassName_##className>

// The registrar's constructor body is a complete-class context, so it can name the method
// before the method itself is declared. Inline static members run before main().
#define TEST_METHOD(methodName) \
    struct HcRegister_##methodName \
    { \
        HcRegister_##methodName() \
        { \
            ::hcunittest::RegisterTest(HcThisClass::HcClassName(), #methodName, [] { HcThisClass::HcRun(&HcThisClass::methodName); }); \
        } \
    }; \
    static inline HcRegister_##methodName s_hcRegister_##methodName{}; \
    void methodName()

#define TEST_METHOD_SETUP(methodName) void HcMethodSetup() override { methodName(); } void methodName()
#define TEST_METHOD_INITIALIZE(methodName) TEST_METHOD_SETUP(methodName)
#define TEST_METHOD_CLEANUP(methodName) void HcMethodCleanup() override { methodName(); } void methodName()

#define VERIFY_ARE_EQUAL(expected, actual) ::hcunittest::VerifyAreEqual((expected), (actual), true, #expected, #actual, __FILE__, __LINE__)
#define VERIFY_ARE_NOT_EQUAL(expected, actual) ::hcunittest::VerifyAreEqual((expected), (actual), false, #expected, #actual, __FILE__, __LINE__)
#define VERIFY_ARE_EQUAL_INT(expected, actual) VERIFY_ARE_EQUAL(static_cast<int64_t>(expected), static_cast<int64_t>(actual))
#define VERIFY_ARE_EQUAL_UINT(expected, actual) VERIFY_ARE_EQUAL(static_cast<uint64_t>(expected), static_cast<uint64_t>(actual))
#define VERIFY_ARE_EQUAL_STR(expected, actual) ::hcunittest::VerifyStringsEqual((expected), (actual), false, #expected, #actual, __FILE__, __LINE__)
#define VERIFY_ARE_EQUAL_STR_IGNORE_CASE(expected, actual) ::hcunittest::VerifyStringsEqual((expected), (actual), true, #expected, #actual, __FILE__, __LINE__)
// Matches the TE backend: VERIFY_SUCCEEDED requires exactly S_OK.
#define VERIFY_SUCCEEDED(x) ::hcunittest::VerifyAreEqual(static_cast<HRESULT>(S_OK), static_cast<HRESULT>(x), true, "S_OK", #x, __FILE__, __LINE__)
#define VERIFY_FAILED(x) ::hcunittest::VerifyCondition(FAILED(static_cast<HRESULT>(x)), "VERIFY_FAILED", #x, __FILE__, __LINE__)
#define VERIFY_FAIL() ::hcunittest::Fail(__FILE__, __LINE__, "VERIFY_FAIL()")
#define VERIFY_IS_TRUE(x) ::hcunittest::VerifyCondition(!!(x), "VERIFY_IS_TRUE", #x, __FILE__, __LINE__)
#define VERIFY_IS_FALSE(x) ::hcunittest::VerifyCondition(!(x), "VERIFY_IS_FALSE", #x, __FILE__, __LINE__)
#define VERIFY_IS_NULL(x) ::hcunittest::VerifyCondition((x) == nullptr, "VERIFY_IS_NULL", #x, __FILE__, __LINE__)
#define VERIFY_IS_NOT_NULL(x) ::hcunittest::VerifyCondition((x) != nullptr, "VERIFY_IS_NOT_NULL", #x, __FILE__, __LINE__)
#define VERIFY_IS_LESS_THAN(a, b) ::hcunittest::VerifyCondition((a) < (b), "VERIFY_IS_LESS_THAN", #a ", " #b, __FILE__, __LINE__)
#define VERIFY_IS_GREATER_THAN_OR_EQUAL(a, b) ::hcunittest::VerifyCondition((a) >= (b), "VERIFY_IS_GREATER_THAN_OR_EQUAL", #a ", " #b, __FILE__, __LINE__)
#define VERIFY_NO_THROW(operation, ...) { try { operation; } catch (...) { ::hcunittest::Fail(__FILE__, __LINE__, "VERIFY_NO_THROW(" #operation ")"); } }

// TE's LOG_COMMENT is a no-op; keep it that way so wide printf formats (%ws, %I64u) never matter.
#define LOG_COMMENT(x, ...)
