// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/config.h>
#include <httpClient/pal.h>

#if (!defined(HC_LINK_STATIC) || HC_LINK_STATIC == 0) && HC_PLATFORM_IS_APPLE
#include <httpClient/XAsync.h>
#include <httpClient/XAsyncProvider.h>
#include <httpClient/XTaskQueue.h>
#else
#include <XAsync.h>
#include <XAsyncProvider.h>
#include <XTaskQueue.h>
#endif

#if HC_PLATFORM == HC_PLATFORM_ANDROID
#include <httpClient/async_jvm.h>
#endif