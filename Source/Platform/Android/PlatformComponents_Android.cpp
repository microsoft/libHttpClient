#include "pch.h"
#include "Platform/Android/PlatformComponents_Android.h"
#include "HTTP/Android/AndroidHttpProvider.h"
#include "WebSocket/Android/AndroidWebSocketProvider.h"
#include "HTTP/Android/http_android.h"

// Java package (in JNI path form) that holds libHttpClient's Java classes. A
// private copy of libHttpClient embedded in another shared library can relocate
// its Java classes to a different package so they don't collide with the
// public copy. Native methods are bound with RegisterNatives rather than by
// exported symbol name, so they resolve correctly in either package.
#ifndef HC_ANDROID_JAVA_PACKAGE_PATH
#define HC_ANDROID_JAVA_PACKAGE_PATH "com/xbox/httpclient"
#endif
#define HC_JAVA_CLASS(name) HC_ANDROID_JAVA_PACKAGE_PATH "/" name

#ifndef HC_NOWEBSOCKETS
extern "C"
{
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onOpen(JNIEnv*, jobject);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onFailure(JNIEnv*, jobject, jint);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onClose(JNIEnv*, jobject, jint);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onMessage(JNIEnv*, jobject, jstring);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onBinaryMessage(JNIEnv*, jobject, jobject);
}
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace
{

HRESULT RegisterNativeMethods(JNIEnv* jniEnv, jclass clazz, const char* className, const JNINativeMethod* methods, jint count) noexcept
{
    if (clazz == nullptr || jniEnv->RegisterNatives(clazz, methods, count) != JNI_OK)
    {
        if (jniEnv->ExceptionCheck())
        {
            jniEnv->ExceptionClear();
        }
        HC_TRACE_ERROR(HTTPCLIENT, "Could not register native methods for %s", className);
        return E_FAIL;
    }
    return S_OK;
}

HRESULT RegisterAllNativeMethods(
    JNIEnv* jniEnv,
    jclass networkObserverClass,
    jclass requestClass,
    jclass webSocketClass
) noexcept
{
    const JNINativeMethod networkObserverMethods[] = {
        { "Log", "(Ljava/lang/String;)V", reinterpret_cast<void*>(&Java_com_xbox_httpclient_NetworkObserver_Log) },
    };
    RETURN_IF_FAILED(RegisterNativeMethods(jniEnv, networkObserverClass, "NetworkObserver", networkObserverMethods, 1));

    const JNINativeMethod requestMethods[] = {
        { "OnRequestCompleted", "(JL" HC_JAVA_CLASS("HttpClientResponse") ";)V", reinterpret_cast<void*>(&Java_com_xbox_httpclient_HttpClientRequest_OnRequestCompleted) },
        { "OnRequestFailed", "(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V", reinterpret_cast<void*>(&Java_com_xbox_httpclient_HttpClientRequest_OnRequestFailed) },
        { "ReportProgress", "(JJJZ)V", reinterpret_cast<void*>(&Java_com_xbox_httpclient_HttpClientRequest_ReportProgress) },
    };
    RETURN_IF_FAILED(RegisterNativeMethods(jniEnv, requestClass, "HttpClientRequest", requestMethods, 3));

    jclass requestBodyStreamClass = jniEnv->FindClass(HC_JAVA_CLASS("HttpClientRequestBody$NativeInputStream"));
    const JNINativeMethod requestBodyStreamMethods[] = {
        { "nativeRead", "(JJ[BJJ)I", reinterpret_cast<void*>(&Java_com_xbox_httpclient_HttpClientRequestBody_00024NativeInputStream_nativeRead) },
    };
    HRESULT hr = RegisterNativeMethods(jniEnv, requestBodyStreamClass, "HttpClientRequestBody$NativeInputStream", requestBodyStreamMethods, 1);
    if (requestBodyStreamClass != nullptr)
    {
        jniEnv->DeleteLocalRef(requestBodyStreamClass);
    }
    RETURN_IF_FAILED(hr);

    jclass responseStreamClass = jniEnv->FindClass(HC_JAVA_CLASS("HttpClientResponse$NativeOutputStream"));
    const JNINativeMethod responseStreamMethods[] = {
        { "nativeWrite", "(J[BII)V", reinterpret_cast<void*>(&Java_com_xbox_httpclient_HttpClientResponse_00024NativeOutputStream_nativeWrite) },
    };
    hr = RegisterNativeMethods(jniEnv, responseStreamClass, "HttpClientResponse$NativeOutputStream", responseStreamMethods, 1);
    if (responseStreamClass != nullptr)
    {
        jniEnv->DeleteLocalRef(responseStreamClass);
    }
    RETURN_IF_FAILED(hr);

#ifndef HC_NOWEBSOCKETS
    const JNINativeMethod webSocketMethods[] = {
        { "onOpen", "()V", reinterpret_cast<void*>(&::Java_com_xbox_httpclient_HttpClientWebSocket_onOpen) },
        { "onFailure", "(I)V", reinterpret_cast<void*>(&::Java_com_xbox_httpclient_HttpClientWebSocket_onFailure) },
        { "onClose", "(I)V", reinterpret_cast<void*>(&::Java_com_xbox_httpclient_HttpClientWebSocket_onClose) },
        { "onMessage", "(Ljava/lang/String;)V", reinterpret_cast<void*>(&::Java_com_xbox_httpclient_HttpClientWebSocket_onMessage) },
        { "onBinaryMessage", "(Ljava/nio/ByteBuffer;)V", reinterpret_cast<void*>(&::Java_com_xbox_httpclient_HttpClientWebSocket_onBinaryMessage) },
    };
    RETURN_IF_FAILED(RegisterNativeMethods(jniEnv, webSocketClass, "HttpClientWebSocket", webSocketMethods, 5));
#else
    UNREFERENCED_PARAMETER(webSocketClass);
#endif

    return S_OK;
}

} // anonymous namespace

Result<std::shared_ptr<PlatformComponents_Android>> PlatformComponents_Android::Initialize(HCInitArgs* args) noexcept
{
    assert(args != nullptr);
    JavaVM* javaVm = args->javaVM;
    JNIEnv* jniEnv = nullptr;

    // Pass the jvm down to XTaskQueue
    XTaskQueueSetJvm(javaVm);

    // Java classes can only be resolved when we are on a Java-initiated thread. When we are on
    // a C++ background thread and attach to Java we do not have the full class-loader information.
    // This call should be made on JNI_OnLoad or another java thread and we will cache a global reference
    // to the classes we will use for making HTTP requests.
    jint result = javaVm->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

    if (result != JNI_OK)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Failed to initialize because JavaVM is not attached to a java thread.");
        return E_FAIL;
    }

    // Get class references we need to hold onto

    jclass localNetworkObserver = jniEnv->FindClass(HC_JAVA_CLASS("NetworkObserver"));
    if (localNetworkObserver == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find NetworkObserver class");
        return E_FAIL;
    }

    jclass localHttpRequest = jniEnv->FindClass(HC_JAVA_CLASS("HttpClientRequest"));
    if (localHttpRequest == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest class");
        return E_FAIL;
    }

    jclass localHttpResponse = jniEnv->FindClass(HC_JAVA_CLASS("HttpClientResponse"));
    if (localHttpResponse == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientResponse class");
        return E_FAIL;
    }

    jclass localWebSocket = jniEnv->FindClass(HC_JAVA_CLASS("HttpClientWebSocket"));
    if (localWebSocket == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientWebSocket class");
        return E_FAIL;
    }

    jclass globalNetworkObserver = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localNetworkObserver));
    jclass globalRequestClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpRequest));
    jclass globalResponseClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpResponse));
    jclass globalWebSocketClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localWebSocket));

    RETURN_IF_FAILED(RegisterAllNativeMethods(jniEnv, globalNetworkObserver, globalRequestClass, globalWebSocketClass));

    // Initialize the network observer

    jmethodID networkObserverInitialize = jniEnv->GetStaticMethodID(globalNetworkObserver, "Initialize", "(Landroid/content/Context;)V");
    if (networkObserverInitialize == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find NetworkObserver.Initialize method");
        return E_FAIL;
    }

    jniEnv->CallStaticVoidMethod(globalNetworkObserver, networkObserverInitialize, args->applicationContext);

    // Initialize the context

    try
    {
        http_stl_allocator<PlatformComponents_Android> a{};
        auto platformComponents = std::shared_ptr<PlatformComponents_Android>(
                new (a.allocate(1)) PlatformComponents_Android(
                        javaVm,
                        args->applicationContext,
                        globalNetworkObserver,
                        globalRequestClass,
                        globalResponseClass,
                        globalWebSocketClass
                ), http_alloc_deleter<PlatformComponents_Android>());

        return std::move(platformComponents);
    }
    catch (std::bad_alloc)
    {
        return E_OUTOFMEMORY;
    }
}

PlatformComponents_Android::PlatformComponents_Android(
    JavaVM* javaVm,
    jobject applicationContext,
    jclass networkObserverClass,
    jclass requestClass,
    jclass responseClass,
    jclass webSocketClass
) :
    m_javaVm{ javaVm },
    m_applicationContext{ applicationContext },
    m_networkObserverClass{ networkObserverClass },
    m_httpRequestClass{ requestClass },
    m_httpResponseClass{ responseClass },
    m_webSocketClass{ webSocketClass }
{
    assert(m_javaVm != nullptr);
}

PlatformComponents_Android::~PlatformComponents_Android()
{
    JNIEnv* jniEnv = nullptr;
    bool isThreadAttached = false;
    jint getEnvResult = m_javaVm->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6);

    if (getEnvResult == JNI_EDETACHED)
    {
        jint attachThreadResult = m_javaVm->AttachCurrentThread(&jniEnv, nullptr);

        if (attachThreadResult == JNI_OK)
        {
            isThreadAttached = true;
        }
        else
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Could not attach to java thread to dispose of global class references");
        }
    }

    jmethodID networkObserverCleanup = jniEnv->GetStaticMethodID(m_networkObserverClass, "Cleanup", "(Landroid/content/Context;)V");
    if (networkObserverCleanup == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find NetworkObserver.Cleanup method");
    }
    jniEnv->CallStaticVoidMethod(m_networkObserverClass, networkObserverCleanup, m_applicationContext);

    if (jniEnv != nullptr)
    {
        jniEnv->DeleteGlobalRef(m_networkObserverClass);
        jniEnv->DeleteGlobalRef(m_httpRequestClass);
        jniEnv->DeleteGlobalRef(m_httpResponseClass);
    }

    if (isThreadAttached)
    {
        m_javaVm->DetachCurrentThread();
    }
}

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* args)
{
    auto initAndroidResult = PlatformComponents_Android::Initialize(args);
    RETURN_IF_FAILED(initAndroidResult.hr);

    components.HttpProvider = http_allocate_unique<AndroidHttpProvider>(initAndroidResult.Payload());
#ifndef HC_NOWEBSOCKETS
    components.WebSocketProvider = http_allocate_unique<AndroidWebSocketProvider>(initAndroidResult.Payload());
#endif
    
    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END