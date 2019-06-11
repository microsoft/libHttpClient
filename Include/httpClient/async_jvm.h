// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#if !defined(__cplusplus)
    #error C++11 required
#endif

#include <jni.h>

/// <summary>
/// Set the Java Virtual Machine instance of your app. Currently used only by Android.
/// </summary>
/// <param name="jvm">The pointer to the app's JVM.</param>
/// <returns>Result code for this API operation.  Code appears to always return S_OK.</returns>
STDAPI XTaskQueueSetJvm(_In_ JavaVM* jvm) noexcept;
