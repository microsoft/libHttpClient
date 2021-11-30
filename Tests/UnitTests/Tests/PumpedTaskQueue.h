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
                pthis->cvWork.notify_all();
            }
            else
            {
                pthis->cvCompletion.notify_all();
            }
        }, &token));

        workThread.reset(new std::thread([this] { WorkThreadProc(); }));
        completionThread.reset(new std::thread([this] { CompletionThreadProc(); }));
    }

    ~PumpedTaskQueue()
    {
        shutdown = true;
        cvWork.notify_all();
        cvCompletion.notify_all();

        workThread->join();
        completionThread->join();

        if (queue != nullptr)
        {
            XTaskQueueCloseHandle(queue);
        }
    }

private:

    void WorkThreadProc()
    {
        std::unique_lock<std::mutex> l(workLock);
        while(!shutdown)
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
            cvWork.wait(l);
        }
    }

    void CompletionThreadProc()
    {
        std::unique_lock<std::mutex> l(completionLock);
        while(!shutdown)
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
            cvCompletion.wait(l);
        }
    }

    std::unique_ptr<std::thread> workThread;
    std::unique_ptr<std::thread> completionThread;
    std::mutex workLock;
    std::mutex completionLock;
    bool shutdown = false;
    std::condition_variable cvWork;
    std::condition_variable cvCompletion;
};