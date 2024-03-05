#pragma once

#include "jni.h"

/// <summary>
/// Used to wrap the JavaVM and ApplicationContext on Android devices.
/// </summary>
typedef struct HCInitArgs {
    /// <summary>The Java Virtual machine.</summary>
    JavaVM *javaVM;
    /// <summary>The Java Application Context.</summary>
    jobject applicationContext;
} HCInitArgs;
