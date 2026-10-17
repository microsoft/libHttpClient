#pragma once

#include <jni.h>
#include "Platform/Android/JavaPackage_Android.h"
#include "Platform/IWebSocketProvider.h"

#ifndef HC_NOWEBSOCKETS
// JNI callbacks from com.xbox.httpclient.HttpClientWebSocket, declared here so
// every translation unit that references them agrees on the signatures.
extern "C"
{
    HC_JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onOpen(JNIEnv*, jobject, jobjectArray, jobjectArray);
    HC_JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onFailure(JNIEnv*, jobject, jint, jobjectArray, jobjectArray);
    HC_JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onClose(JNIEnv*, jobject, jint);
    HC_JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onMessage(JNIEnv*, jobject, jstring);
    HC_JNIEXPORT void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onBinaryMessage(JNIEnv*, jobject, jobject);
}
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#ifndef HC_NOWEBSOCKETS
class PlatformComponents_Android;

class AndroidWebSocketProvider : public IWebSocketProvider
{
public:
    AndroidWebSocketProvider(SharedPtr<PlatformComponents_Android> platformComponents);
    AndroidWebSocketProvider(AndroidWebSocketProvider const&) = delete;
    AndroidWebSocketProvider& operator=(AndroidWebSocketProvider const&) = delete;
    virtual ~AndroidWebSocketProvider() = default;

    HRESULT ConnectAsync(
        String const& uri,
        String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept override;

private:
    SharedPtr<PlatformComponents_Android> m_platformComponents;
};
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
