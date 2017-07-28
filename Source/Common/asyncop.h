// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "uwp/utils_uwp.h"

struct http_args
{
    virtual ~http_args() {}
};

struct HC_TASK;

enum http_task_state
{
    pending,
    processing,
    completed
};

struct HC_TASK
{
    std::shared_ptr<http_args> args;
    http_task_state state;
    HC_TASK_FUNC executionRoutine;
    void* executionRoutineContext;
    HC_TASK_FUNC writeResultsRoutine;
    void* writeResultsRoutineContext;
    void* completionRoutine;
    void* completionRoutineContext;
    uint64_t taskGroupId;
    uint64_t id;

#if UWP_API || UNITTEST_API
    win32_handle resultsReady;
#endif
};

void http_task_queue_pending(_In_ std::shared_ptr<HC_TASK> info);

std::shared_ptr<HC_TASK> http_task_get_next_pending();
