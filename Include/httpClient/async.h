// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/config.h>
#include <httpClient/pal.h>

#include <XAsync.h>
#include <XAsyncProvider.h>
#include <XTaskQueue.h>

#if HC_PLATFORM == HC_PLATFORM_ANDROID
#include <httpClient/async_jvm.h>
#endif