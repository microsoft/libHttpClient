#include "pch.h"
#include "Platform/Android/PlatformComponents_Android.h"
#include "HTTP/Android/AndroidHttpProvider.h"
#include "WebSocket/Android/AndroidWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

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

    jclass localNetworkObserver = jniEnv->FindClass("com/xbox/httpclient/NetworkObserver");
    if (localNetworkObserver == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find NetworkObserver class");
        return E_FAIL;
    }

    jclass localHttpRequest = jniEnv->FindClass("com/xbox/httpclient/HttpClientRequest");
    if (localHttpRequest == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientRequest class");
        return E_FAIL;
    }

    jclass localHttpResponse = jniEnv->FindClass("com/xbox/httpclient/HttpClientResponse");
    if (localHttpResponse == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientResponse class");
        return E_FAIL;
    }

    jclass localWebSocket = jniEnv->FindClass("com/xbox/httpclient/HttpClientWebSocket");
    if (localWebSocket == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Could not find HttpClientWebSocket class");
        return E_FAIL;
    }

    jclass globalNetworkObserver = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localNetworkObserver));
    jclass globalRequestClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpRequest));
    jclass globalResponseClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localHttpResponse));
    jclass globalWebSocketClass = reinterpret_cast<jclass>(jniEnv->NewGlobalRef(localWebSocket));

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
#if !HC_NOWEBSOCKETS
    components.WebSocketProvider = http_allocate_unique<AndroidWebSocketProvider>(initAndroidResult.Payload());
#endif
    
    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END