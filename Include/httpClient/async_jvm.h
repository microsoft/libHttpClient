#pragma once

#if !defined(__cplusplus)
    #error C++11 required
#endif

#include <jni.h>

STDAPI XTaskQueueSetJvm(_In_ JavaVM* jvm) noexcept;
