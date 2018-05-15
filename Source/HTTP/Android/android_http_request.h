#pragma once

#include "jni.h"

class HttpRequest {
public:

    static void InitializeJavaEnvironment(JavaVM* javaVM);
    static void CleanupJavaEnvironment();
private:
    static JavaVM* s_javaVM;
    static jclass s_httpRequestClass;
    static jclass s_httpResponseClass;
};