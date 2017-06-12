// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "singleton.h"
#include "log.h"

#define DEFAULT_TIMEOUT_WINDOW_IN_SECONDS 60

static std::mutex g_httpSingletonLock;
static std::unique_ptr<http_singleton> g_httpSingleton;

http_singleton::http_singleton() 
{
    m_threadPool = std::make_unique<http_thread_pool>();
    m_threadPool->start_threads();
    m_loggingHandlersCounter = 0;
    m_performFunc = nullptr;

    m_traceLevel = HC_DIAGNOSTICS_TRACE_LEVEL::TRACE_OFF;
    m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    m_enableAssertsForThrottling = true;
}

http_singleton::~http_singleton()
{
    std::lock_guard<std::mutex> guard(g_httpSingletonLock);
    g_httpSingleton = nullptr;
}

http_singleton*
get_http_singleton(_In_ bool createIfRequired)
{
    if (createIfRequired)
    {
        std::lock_guard<std::mutex> guard(g_httpSingletonLock);
        if (g_httpSingleton == nullptr)
        {
            g_httpSingleton = std::make_unique<http_singleton>();
        }
    }

    return g_httpSingleton.get();
}

void http_singleton::_Raise_logging_event(_In_ xbox_services_diagnostics_trace_level level, _In_ const std::string& category, _In_ const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_loggingWriteLock);

    for (auto& handler : m_loggingHandlers)
    {
        http_ASSERT(handler.second != nullptr);
        if (handler.second != nullptr)
        {
            try
            {
                handler.second(level, category, message);
            }
            catch (...)
            {
            }
        }
    }
}

function_context http_singleton::add_logging_handler(_In_ std::function<void(xbox_services_diagnostics_trace_level, const std::string&, const std::string&)> handler)
{
    std::lock_guard<std::mutex> lock(m_loggingWriteLock);

    function_context context = -1;
    if (handler != nullptr)
    {
        context = ++m_loggingHandlersCounter;
        m_loggingHandlers[m_loggingHandlersCounter] = std::move(handler);
    }

    return context;
}

void http_singleton::remove_logging_handler(_In_ function_context context)
{
    std::lock_guard<std::mutex> lock(m_loggingWriteLock);
    m_loggingHandlers.erase(context);
}

HC_API double HC_CALLING_CONV
HCGlobalGetLibVersion()
{
    return 6.0f;
}

HC_API void HC_CALLING_CONV
HCGlobalInitialize()
{
    get_http_singleton(true);
}

HC_API void HC_CALLING_CONV
HCGlobalCleanup()
{
    std::lock_guard<std::mutex> guard(g_httpSingletonLock);
    g_httpSingleton = nullptr;
}

void VerifyGlobalInit()
{
    if (g_httpSingleton == nullptr)
    {
        LOG_ERROR("Call HCGlobalInitialize() first");
        assert(g_httpSingleton != nullptr);
    }
}

HC_API void HC_CALLING_CONV
HCGlobalSetHttpCallPerformCallback(
    _In_opt_ HC_HTTP_CALL_PERFORM_FUNC performFunc
    )
{
    VerifyGlobalInit();
    get_http_singleton()->m_performFunc = performFunc;
}


