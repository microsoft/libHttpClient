// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#if HC_UNITTEST_API
#include "httpClient/types.h"
#include "httpClient/httpClient.h"
#include "../global/global.h"
#include "../task/task_impl.h"

void Internal_HCHttpCallPerform(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    HCTaskSetCompleted(taskHandle);
}
#endif