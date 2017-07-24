// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../http/httpcall.h"
#include "buildver.h"
#include "singleton.h"

static const uint32_t DEFAULT_TIMEOUT_WINDOW_IN_SECONDS = 20;
static const uint32_t DEFAULT_RETRY_DELAY_IN_SECONDS = 2;

static std::mutex g_httpSingletonLock;
static std::unique_ptr<http_singleton> g_httpSingleton;

http_singleton::http_singleton() 
{
    m_loggingHandlersCounter = 0;
    m_performFunc = Internal_HCHttpCallPerform;

    m_traceLevel = HC_LOG_LEVEL::LOG_OFF;
    m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    m_retryDelayInSeconds = DEFAULT_RETRY_DELAY_IN_SECONDS;
    m_enableAssertsForThrottling = true;
    m_mocksEnabled = false;
    m_lastMatchingMock = nullptr;
}

http_singleton::~http_singleton()
{
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

void http_singleton::_Raise_logging_event(_In_ HC_LOG_LEVEL level, _In_ const std::string& category, _In_ const std::string& message)
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

std::shared_ptr<http_task_completed_queue> http_singleton::get_task_queue_for_id(_In_ uint64_t taskGroupId)
{
    std::lock_guard<std::mutex> lock(m_taskCompletedQueueLock);
    auto it = m_taskCompletedQueue.find(taskGroupId);
    if (it != m_taskCompletedQueue.end())
    {
        return it->second;
    }

    std::shared_ptr<http_task_completed_queue> taskQueue = std::make_shared<http_task_completed_queue>();
    m_taskCompletedQueue[taskGroupId] = taskQueue;
    return taskQueue;
}

function_context http_singleton::add_logging_handler(_In_ std::function<void(HC_LOG_LEVEL, const std::string&, const std::string&)> handler)
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

HC_API void HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR_T* version)
{
    *version = LIBHTTPCLIENT_VERSION;
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
    for (auto& mockCall : g_httpSingleton->m_mocks)
    {
        HCHttpCallCleanup(mockCall);
    }
    g_httpSingleton->m_mocks.clear();

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

http_internal_string SetOptionalParam(_In_opt_ PCSTR_T param)
{
    if (param == nullptr)
    {
        return _T("");
    }
    else
    {
        return param;
    }
}

HC_API void HC_CALLING_CONV
HCGlobalSetHttpCallPerformFunction(
    _In_opt_ HC_HTTP_CALL_PERFORM_FUNC performFunc
    )
{
    VerifyGlobalInit();
    get_http_singleton()->m_performFunc = (performFunc == nullptr) ? Internal_HCHttpCallPerform : performFunc;
}

HC_API void HC_CALLING_CONV
HCGlobalGetHttpCallPerformFunction(
    _Out_ HC_HTTP_CALL_PERFORM_FUNC* performFunc
    )
{
    VerifyGlobalInit();
    *performFunc = get_http_singleton()->m_performFunc;
}

#if UWP_API || UNITTEST_API
HANDLE http_singleton::get_pending_ready_handle()
{
    return m_pendingReadyHandle.get();
}

void http_singleton::set_async_op_pending_ready()
{
    SetEvent(get_pending_ready_handle());
}
#endif

#if UWP_API || UNITTEST_API
HANDLE http_task_completed_queue::get_complete_ready_handle()
{
    return m_completeReadyHandle.get();
}

void http_task_completed_queue::set_async_op_complete_ready()
{
    SetEvent(get_complete_ready_handle());
}
#endif

http_internal_queue<std::shared_ptr<HC_ASYNC_INFO>> http_task_completed_queue::get_queue()
{
    return m_asyncCompleteQueue;
}

