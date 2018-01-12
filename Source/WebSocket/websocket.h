// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

struct HC_WEBSOCKET
{
    HC_WEBSOCKET() :
        id(0),
        refCount(1),
        connectCalled(false)
    {
    }

    uint64_t id;
    std::atomic<int> refCount;
    bool connectCalled;
    http_internal_map<http_internal_string, http_internal_string> connectHeaders;
    http_internal_string proxyUri;
};

HC_RESULT Internal_HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CONNECT_INIT_ARGS args,
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCWebSocketCompletionRoutine completionRoutine
    );

HC_RESULT Internal_HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message,
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCWebSocketCompletionRoutine completionRoutine
    );

HC_RESULT Internal_HCWebSocketClose(
    _In_ HC_WEBSOCKET_HANDLE websocket
    );

