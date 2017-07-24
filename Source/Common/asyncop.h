// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "uwp/utils_uwp.h"

struct http_args
{
    virtual ~http_args() {}
};

struct HC_ASYNC_INFO;

enum http_async_state
{
    pending,
    processing,
    completed
};

struct HC_ASYNC_INFO
{
    std::shared_ptr<http_args> args;
    http_async_state state;
    HC_ASYNC_OP_FUNC executionRoutine;
    void* executionRoutineContext;
    HC_ASYNC_OP_FUNC writeResultsRoutine;
    void* writeResultsRoutineContext;
    void* completionRoutine;
    void* completionRoutineContext;
    uint64_t taskGroupId;

#if UWP_API || UNITTEST_API
    win32_handle resultsReady;
#endif
};

void http_asyncop_push_pending_asyncop(_In_ std::shared_ptr<HC_ASYNC_INFO> info);

std::shared_ptr<HC_ASYNC_INFO> http_asyncop_get_next_pending_async_op();
