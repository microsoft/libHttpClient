#include "android_http_request.h"

/* static */ JavaVM* HttpRequest::s_javaVM = nullptr;
/* static */ jclass HttpRequest::s_httpRequestClass = nullptr;
/* static */ jclass HttpRequest::s_httpResponseClass = nullptr;

/* static */ void HttpRequest::InitializeJavaEnvironment(JavaVM* javaVM) {
    // TODO: Return an HRESULT or another error type?
    s_javaVM = javaVM;
    JNIEnv* jniEnv = nullptr;
    jint threadAttached = s_javaVM->AttachCurrentThread(&jniEnv, nullptr);

    // TODO: Check that the thread is attached.
    // TODO: Check that class was found.

    // Java classes can only be resolved when we are on a Java-initiated thread; when we are on
    // a C++ background thread and attach to Java we do not have the full class-loader information.
    // This call should be made on JNI_Onload or another java thread and we will cache a global reference
    // to the classes we will use for making HTTP requests.
    jclass localHttpRequest = jniEnv->FindClass("com/xbox/httpclient/HttpClientRequest");
    jclass localHttpResponse = jniEnv->FindClass("com/xbox/httpclient/HttpClientResponse");

    s_httpRequestClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpRequest));
    s_httpResponseClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpResponse));   
    s_javaVM->DetachCurrentThread();
}

/* static */ void HttpRequest::CleanupJavaEnvironment() {
    JNIEnv* jniEnv = nullptr;
    jint threadAttached = s_javaVM->AttachCurrentThread(&jniEnv, nullptr);

    jniEnv->DeleteGlobalRef(s_httpRequestClass);
    jniEnv->DeleteGlobalRef(s_httpResponseClass);

    s_javaVM->DetachCurrentThread();
}