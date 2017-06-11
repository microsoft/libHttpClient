// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "threadpool.h"
#include "asyncop.h"
#include "mem.h"
#include "utils.h"


struct http_singleton
{
    http_singleton();
    ~http_singleton();

    std::mutex m_singletonLock;

    std::mutex m_asyncLock;
    http_internal_queue(std::shared_ptr<http_async_info>) m_asyncPendingQueue;

    std::unique_ptr<http_thread_pool> m_threadPool;

    std::function<_Ret_maybenull_ _Post_writable_byte_size_(dwSize) void*(_In_ size_t dwSize)> m_pMemAllocHook;
    std::function<void(_In_ void* pAddress)> m_pMemFreeHook;

    function_context add_logging_handler(_In_ std::function<void(xbox_services_diagnostics_trace_level, const std::string&, const std::string&)> handler);
    void remove_logging_handler(_In_ function_context context);
    void _Raise_logging_event(_In_ xbox_services_diagnostics_trace_level level, _In_ const std::string& category, _In_ const std::string& message);

    std::mutex m_loggingWriteLock;
    std::unordered_map<function_context, std::function<void(xbox_services_diagnostics_trace_level, const std::string&, const std::string&)>> m_loggingHandlers;
    function_context m_loggingHandlersCounter;

    HC_HTTP_CALL_PERFORM_FUNC m_performFunc;
    HC_DIAGNOSTICS_TRACE_LEVEL m_traceLevel;
    uint32_t m_timeoutWindowInSeconds;
    bool m_enableAssertsForThrottling;
};

http_singleton* get_http_singleton(_In_ bool createIfRequired = false);
void VerifyGlobalInit();


