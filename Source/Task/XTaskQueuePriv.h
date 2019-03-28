// Copyright(c) Microsoft Corporation. All rights reserved.
//
// These APIs should be reserved for driving unit test harnesses.

#pragma once

#include "XTaskQueue.h"

/// <summary>
/// Returns TRUE if there is no outstanding work in this
/// queue for the given callback port.
/// </summary>
/// <param name='queue'>The queue to check.</param>
/// <param name='port'>The port to check.</param>
STDAPI_(bool) XTaskQueueIsEmpty(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port);
