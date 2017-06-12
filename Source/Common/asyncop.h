// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include "httpClient/httpClient.h"

struct http_args
{
    virtual ~http_args() {}
};

struct http_async_info;

typedef void(* http_async_op_execute_routine)(
    _In_ const std::shared_ptr<http_async_info>& info
    );

enum http_async_state
{
    pending,
    processing,
    completed
};

struct http_async_info
{
    void* completionRoutineContext;
    std::shared_ptr<http_args> args;
    http_async_state state;
    void* completionRoutine;
    http_async_op_execute_routine executeRoutine;
};

void http_asyncop_set_info_in_new_handle( 
    _In_ std::shared_ptr<http_async_info> info,
    _In_ void* completionRoutineContext,
    _In_ void* completionRoutine
    );

std::shared_ptr<http_async_info> http_asyncop_get_next_async_op();
