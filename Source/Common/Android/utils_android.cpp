// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "utils_android.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// TODO this code needs reworking as it does not quite do the right thing at the moment
std::atomic<JavaVM*> JVM{ nullptr };

static void abort_if_no_jvm()
{
    if (JVM == nullptr)
    {
        std::abort();
    }
}

JNIEnv* get_jvm_env()
{
    abort_if_no_jvm();
    JNIEnv* env = nullptr;
    auto result = JVM.load()->AttachCurrentThread(&env, nullptr);
    if (result != JNI_OK)
    {
        throw std::runtime_error("Could not attach to JVM");
    }

    return env;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
