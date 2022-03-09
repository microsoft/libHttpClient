// Copyright(c) Microsoft Corporation. All rights reserved.

#pragma once

#include <thread>
#include <mutex>
#include <memory>

class PumpedTaskQueue
{
public:

    XTaskQueueHandle queue = nullptr;

    PumpedTaskQueue()
    {
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        XTaskQueueRegistrationToken token;
        VERIFY_SUCCEEDED(XTaskQueueRegisterMonitor(queue, this, [](void* cxt, XTaskQueueHandle, XTaskQueuePort port)
        {
            PumpedTaskQueue* pthis = (PumpedTaskQueue*)cxt;
            if (port == XTaskQueuePort::Work)
            {
                pthis->Signal(pthis->workData);
            }
            else
            {
                pthis->Signal(pthis->completionData);
            }
        }, &token));

        workThread.reset(new std::thread([this] { WorkThreadProc(); }));
        completionThread.reset(new std::thread([this] { CompletionThreadProc(); }));
    }

    ~PumpedTaskQueue()
    {
        Shutdown(workData);
        Shutdown(completionData);

        workThread->join();
        completionThread->join();

        if (queue != nullptr)
        {
            XTaskQueueCloseHandle(queue);
        }
    }

private:

    struct NotifyData
    {
        std::mutex lock;
        std::condition_variable cv;
        bool notify = false;
    };

    bool WaitForNotify(NotifyData& notifyData)
    {
        std::unique_lock<std::mutex> l(notifyData.lock);

        while (true)
        {
            if (shutdown)
            {
                return false;
            }

            if (notifyData.notify)
            {
                notifyData.notify = false;
                return true;
            }

            notifyData.cv.wait(l);
        }
    }

    void Signal(NotifyData& notifyData)
    {
        std::unique_lock<std::mutex> l(notifyData.lock);
        notifyData.notify = true;
        notifyData.cv.notify_all();
    }

    void Shutdown(NotifyData& notifyData)
    {
        std::unique_lock<std::mutex> l(notifyData.lock);
        shutdown = true;
        notifyData.cv.notify_all();
    }

    void WorkThreadProc()
    {
        while (WaitForNotify(workData))
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
        }
    }

    void CompletionThreadProc()
    {
        while (WaitForNotify(completionData))
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
        }
    }

    std::unique_ptr<std::thread> workThread;
    std::unique_ptr<std::thread> completionThread;
    NotifyData workData;
    NotifyData completionData;
    bool shutdown = false;
};