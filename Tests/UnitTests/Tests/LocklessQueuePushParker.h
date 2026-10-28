// Copyright(c) Microsoft Corporation. All rights reserved.

#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include "LocklessQueue.h"

// Parks one opted-in thread inside LocklessQueue's push, after it has read the
// tail's end marker and before it links its node (LocklessQueueTestHooks), so a
// test can recycle that tail node through other lists sharing the node heap.
// Only the thread that calls ArmCurrentThread is parked, and only once.
class LocklessQueuePushParker
{
public:

    LocklessQueuePushParker()
    {
        Instance() = this;
        LocklessQueueTestHooks::BeforeTailLink() = &LocklessQueuePushParker::OnBeforeTailLink;
    }

    ~LocklessQueuePushParker()
    {
        Release();
        LocklessQueueTestHooks::BeforeTailLink() = nullptr;
        Instance() = nullptr;
    }

    static void ArmCurrentThread()
    {
        ThreadArmed() = true;
    }

    bool WaitUntilParked(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        return m_cv.wait_for(lock, timeout, [this] { return m_parked; });
    }

    void Release()
    {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_release = true;
        }
        m_cv.notify_all();
    }

private:

    static LocklessQueuePushParker*& Instance()
    {
        static LocklessQueuePushParker* instance = nullptr;
        return instance;
    }

    static bool& ThreadArmed()
    {
        static thread_local bool armed = false;
        return armed;
    }

    static void OnBeforeTailLink()
    {
        LocklessQueuePushParker* parker = Instance();
        if (!ThreadArmed() || parker == nullptr)
        {
            return;
        }

        ThreadArmed() = false;

        std::unique_lock<std::mutex> lock(parker->m_lock);
        parker->m_parked = true;
        parker->m_cv.notify_all();
        parker->m_cv.wait_for(lock, std::chrono::seconds(30), [parker] { return parker->m_release; });
    }

    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_parked = false;
    bool m_release = false;
};
