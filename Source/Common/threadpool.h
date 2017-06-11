// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"

class win32_handle
{
public:
    win32_handle() : m_handle(nullptr)
    {
    }

    ~win32_handle()
    {
        if( m_handle != nullptr ) CloseHandle(m_handle);
        m_handle = nullptr;
    }

    void set(HANDLE handle)
    {
        m_handle = handle;
    }

    HANDLE get() { return m_handle; }

private:
    HANDLE m_handle;
};

class http_thread_pool
{
public:
    http_thread_pool();
        
    void start_threads();

    void set_target_num_active_threads(_In_ uint32_t targetNumThreads);
    void shutdown_active_threads();
    long get_num_active_threads();
    void set_thread_ideal_processor(_In_ int threadIndex, _In_ DWORD dwIdealProcessor);

    HANDLE get_stop_handle();
    HANDLE get_ready_handle();
    void set_async_op_ready();

private:
    uint32_t m_targetNumThreads;
    win32_handle m_stopRequestedHandle;
    win32_handle m_readyHandle;

    long m_numActiveThreads;
    HANDLE m_hActiveThreads[64];
    DWORD m_defaultIdealProcessor;
};
