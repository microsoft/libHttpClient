#include "pch.h"

#if !HC_NOWEBSOCKETS

#include "jni.h"

#include "HTTP/Android/android_platform_context.h"
#include "../hcwebsocket.h"
#include "okhttp_websocket.h"

#include "httpClient.h"
#include "httpProvider.h"

extern "C"
{
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onOpen(JNIEnv*, jobject);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onFailure(JNIEnv*, jobject);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onClose(JNIEnv*, jobject, jint);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onMessage(JNIEnv*, jobject, jstring);
    void JNICALL Java_com_xbox_httpclient_HttpClientWebSocket_onBinaryMessage(JNIEnv*, jobject, jobject);
}

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

struct okhttp_websocket_impl;

struct HttpClientWebSocket
{
    static okhttp_websocket_impl* GetOwner(JNIEnv* env, jobject webSocketInstance)
    {
        if (!env || !webSocketInstance)
        {
            return nullptr;
        }

        const jclass webSocketClass = env->GetObjectClass(webSocketInstance);
        if (!webSocketClass)
        {
            return nullptr;
        }

        const jfieldID ownerField = GetOwnerField(env, webSocketClass);
        if (!ownerField)
        {
            return nullptr;
        }

        const jlong owner = env->GetLongField(webSocketInstance, ownerField);
        return reinterpret_cast<okhttp_websocket_impl*>(owner);
    }

    HttpClientWebSocket(JavaVM* vm, jclass webSocketClass, okhttp_websocket_impl* owner)
        : m_vm(vm)
        , m_addHeader(GetAddHeaderMethod(GetEnv(vm), webSocketClass))
        , m_connect(GetConnectMethod(GetEnv(vm), webSocketClass))
        , m_sendMessage(GetSendMessageMethod(GetEnv(vm), webSocketClass))
        , m_sendBinaryMessage(GetSendBinaryMessageMethod(GetEnv(vm), webSocketClass))
        , m_disconnect(GetDisconnectMethod(GetEnv(vm), webSocketClass))
        , m_webSocket(CreateWebSocketInstance(GetEnv(vm), webSocketClass, owner)) {}

    ~HttpClientWebSocket()
    {
        if (JNIEnv* env = GetEnv(m_vm))
        {
            env->DeleteGlobalRef(m_webSocket);
        }
    }

    HRESULT AddHeader(const char* name, const char* value) const
    {
        if (!name || !value)
        {
            return E_INVALIDARG;
        }

        JNIEnv* env = GetEnv(m_vm);
        if (!env || !m_webSocket || !m_addHeader)
        {
            return E_UNEXPECTED;
        }

        const jstring headerName = env->NewStringUTF(name);
        if (HadException(env) || !headerName)
        {
            return E_UNEXPECTED;
        }

        const jstring headerValue = env->NewStringUTF(value);
        if (HadException(env) || !headerValue)
        {
            return E_UNEXPECTED;
        }

        env->CallVoidMethod(m_webSocket, m_addHeader, headerName, headerValue);
        if (HadException(env))
        {
            return E_UNEXPECTED;
        }

        return S_OK;
    }

    HRESULT Connect(const std::string& uri, const std::string subProtocol) const
    {
        if (uri.empty())
        {
            return E_INVALIDARG;
        }

        JNIEnv* env = GetEnv(m_vm);
        if (!env || !m_webSocket || !m_connect)
        {
            return E_UNEXPECTED;
        }

        const jstring javaUri = env->NewStringUTF(uri.c_str());
        if (HadException(env) || !javaUri)
        {
            return E_UNEXPECTED;
        }

        const jstring javaSubProtocol = env->NewStringUTF(subProtocol.c_str());
        if (HadException(env) || !javaSubProtocol)
        {
            return E_UNEXPECTED;
        }

        env->CallVoidMethod(m_webSocket, m_connect, javaUri, javaSubProtocol);
        if (HadException(env))
        {
            return E_UNEXPECTED;
        }

        return S_OK;
    }

    HRESULT SendMessage(const std::string& message) const
    {
        if (message.empty())
        {
            return S_OK;
        }

        JNIEnv* env = GetEnv(m_vm);
        if (!env || !m_webSocket || !m_sendMessage)
        {
            return E_UNEXPECTED;
        }

        const jstring javaMessage = env->NewStringUTF(message.c_str());
        if (!javaMessage)
        {
            return E_UNEXPECTED;
        }

        const jboolean result = env->CallBooleanMethod(m_webSocket, m_sendMessage, javaMessage);
        if (HadException(env))
        {
            return E_UNEXPECTED;
        }

        return result ? S_OK : E_FAIL;
    }

    HRESULT SendBinaryMessage(const uint8_t* data, uint32_t dataSize) const
    {
        if (!data)
        {
            return E_INVALIDARG;
        }

        if (dataSize == 0)
        {
            return S_OK;
        }

        JNIEnv* env = GetEnv(m_vm);
        if (!env || !m_webSocket || !m_sendBinaryMessage)
        {
            return E_UNEXPECTED;
        }

        const jobject buffer = env->NewDirectByteBuffer(const_cast<uint8_t*>(data), static_cast<jlong>(dataSize));
        if (HadException(env) || !buffer)
        {
            return E_UNEXPECTED;
        }

        const jboolean result = env->CallBooleanMethod(m_webSocket, m_sendBinaryMessage, buffer);
        if (HadException(env))
        {
            return E_UNEXPECTED;
        }

        return result ? S_OK : E_FAIL;
    }

    HRESULT Disconnect(int closeStatus) const
    {
        JNIEnv* env = GetEnv(m_vm);
        if (!env || !m_webSocket || !m_disconnect)
        {
            return E_UNEXPECTED;
        }

        env->CallVoidMethod(m_webSocket, m_disconnect, static_cast<jint>(closeStatus));
        if (HadException(env))
        {
            return E_UNEXPECTED;
        }

        return S_OK;
    }

private:
    static JNIEnv* GetEnv(JavaVM* vm)
    {
        if (!vm)
        {
            return nullptr;
        }

        void* env = nullptr;
        vm->GetEnv(&env, JNI_VERSION_1_6);

        return static_cast<JNIEnv*>(env);
    }

    static jmethodID GetAddHeaderMethod(JNIEnv* env, jclass webSocketClass)
    {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jmethodID addHeader = env->GetMethodID(webSocketClass, "addHeader","(Ljava/lang/String;Ljava/lang/String;)V");
        if (HadException(env) || !addHeader)
        {
            return nullptr;
        }

        return addHeader;
    }

    static jmethodID GetConnectMethod(JNIEnv* env, jclass webSocketClass)
    {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jmethodID connect = env->GetMethodID(webSocketClass, "connect", "(Ljava/lang/String;Ljava/lang/String;)V");
        if (HadException(env) || !connect)
        {
            return nullptr;
        }

        return connect;
    }

    static jmethodID GetSendMessageMethod(JNIEnv* env, jclass webSocketClass)
    {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jmethodID sendMessage = env->GetMethodID(webSocketClass, "sendMessage", "(Ljava/lang/String;)Z");
        if (HadException(env) || !sendMessage)
        {
            return nullptr;
        }

        return sendMessage;
    }

    static jmethodID GetSendBinaryMessageMethod(JNIEnv* env, jclass webSocketClass) {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jmethodID sendBinaryMessage = env->GetMethodID(webSocketClass, "sendBinaryMessage","(Ljava/nio/ByteBuffer;)Z");
        if (HadException(env) || !sendBinaryMessage)
        {
            return nullptr;
        }

        return sendBinaryMessage;
    }

    static jmethodID GetDisconnectMethod(JNIEnv* env, jclass webSocketClass)
    {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jmethodID disconnect = env->GetMethodID(webSocketClass, "disconnect", "(I)V");
        if (HadException(env) || !disconnect)
        {
            return nullptr;
        }

        return disconnect;
    }

    static jfieldID GetOwnerField(JNIEnv* env, jclass webSocketClass)
    {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jfieldID owner = env->GetFieldID(webSocketClass, "owner", "J");
        if (HadException(env) || !owner)
        {
            return nullptr;
        }

        return owner;
    }

    static jobject CreateWebSocketInstance(JNIEnv* env, jclass webSocketClass, okhttp_websocket_impl* owner)
    {
        if (!env || !webSocketClass)
        {
            return nullptr;
        }

        const jmethodID constructor = env->GetMethodID(webSocketClass, "<init>", "(J)V");
        if (HadException(env) || !constructor)
        {
            return nullptr;
        }

        const jobject localRef = env->NewObject(webSocketClass, constructor, reinterpret_cast<jlong>(owner));
        if (HadException(env) || !localRef)
        {
            return nullptr;
        }

        return env->NewGlobalRef(localRef);
    }

    static bool HadException(JNIEnv* env)
    {
        if (!env)
        {
            return false;
        }

        if (!env->ExceptionCheck())
        {
            return false;
        }

        env->ExceptionClear();
        return true;
    }

private:
    JavaVM* const m_vm;
    const jmethodID m_addHeader;
    const jmethodID m_connect;
    const jmethodID m_sendMessage;
    const jmethodID m_sendBinaryMessage;
    const jmethodID m_disconnect;
    const jobject m_webSocket;
};

struct okhttp_websocket_impl : hc_websocket_impl, std::enable_shared_from_this<okhttp_websocket_impl>
{
    friend void JNICALL ::Java_com_xbox_httpclient_HttpClientWebSocket_onOpen(JNIEnv*, jobject);
    friend void JNICALL ::Java_com_xbox_httpclient_HttpClientWebSocket_onFailure(JNIEnv*, jobject);
    friend void JNICALL ::Java_com_xbox_httpclient_HttpClientWebSocket_onClose(JNIEnv*, jobject, jint);
    friend void JNICALL ::Java_com_xbox_httpclient_HttpClientWebSocket_onMessage(JNIEnv*, jobject, jstring);
    friend void JNICALL ::Java_com_xbox_httpclient_HttpClientWebSocket_onBinaryMessage(JNIEnv*, jobject, jobject);

    okhttp_websocket_impl(JavaVM* vm, jclass webSocketClass, HCWebsocketHandle handle)
        : m_handle(handle)
        , m_javaWebSocket(vm, webSocketClass, this) {}

    ~okhttp_websocket_impl() {}

    HRESULT ConnectAsync(const char* uri, const char* subProtocol, XAsyncBlock* asyncBlock)
    {
        struct CallArguments
        {
            const std::shared_ptr<okhttp_websocket_impl> sharedThis;

            const std::string uri;
            const std::string subProtocol;

            WebSocketCompletionResult completionResult = { 0 };
        };
        std::unique_ptr<CallArguments> context = std::make_unique<CallArguments>(CallArguments{ shared_from_this(), uri, subProtocol });

        return XAsyncBegin(asyncBlock, context.release(), (const void*)HCWebSocketConnectAsync, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data)
        {
            CallArguments* context = static_cast<CallArguments*>(data->context);
            auto& sharedThis = context->sharedThis;

            switch (op)
            {
                case XAsyncOp::Begin:
                    return XAsyncSchedule(data->async, 0);
                case XAsyncOp::DoWork:
                {
                    auto lock = sharedThis->Lock();

                    switch (sharedThis->m_socketState)
                    {
                        case State::Disconnected:
                        {
                            HRESULT hr = sharedThis->Connect(sharedThis->m_socketState, context->uri, context->subProtocol);
                            if (FAILED(hr))
                            {
                                WebSocketCompletionResult& result = context->completionResult;
                                result.websocket = sharedThis->GetHandle();
                                result.errorCode = hr;
                                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                                return S_OK;
                            }

                            sharedThis->m_socketState = State::Connecting;
                        }
                        case State::Connecting:
                        {
                            HRESULT hr = XAsyncSchedule(data->async, 0);
                            if (FAILED(hr))
                            {
                                WebSocketCompletionResult& result = context->completionResult;
                                result.websocket = sharedThis->GetHandle();
                                result.errorCode = hr;
                                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                                return S_OK;
                            }
                            return E_PENDING;
                        }
                        case State::ConnectSucceeded:
                        {
                            sharedThis->m_socketState = State::Connected;

                            WebSocketCompletionResult& result = context->completionResult;
                            result.websocket = sharedThis->GetHandle();
                            result.errorCode = S_OK;
                            XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                            return S_OK;
                        }
                        case State::ConnectFailed:
                        {
                            sharedThis->m_socketState = State::Disconnected;

                            WebSocketCompletionResult& result = context->completionResult;
                            result.websocket = sharedThis->GetHandle();
                            result.errorCode = E_FAIL;
                            XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                            return S_OK;
                        }
                        case State::Connected:
                            return E_UNEXPECTED;
                    }
                }
                case XAsyncOp::GetResult:
                {
                    CallArguments* context = static_cast<CallArguments*>(data->context);

                    if (data->bufferSize < sizeof(WebSocketCompletionResult))
                    {
                        return E_UNEXPECTED;
                    }

                    // copy result
                    WebSocketCompletionResult& asyncResult = *static_cast<WebSocketCompletionResult*>(data->buffer);
                    asyncResult = context->completionResult;
                    return S_OK;
                }
                case XAsyncOp::Cleanup:
                {
                    // re-capture previously allocated memory, and free
                    std::unique_ptr<CallArguments> contextPtr{ context };
                }
                default:
                    return S_OK;
            }
        });
    }

    HRESULT SendMessageAsync(const char* message, XAsyncBlock* asyncBlock) const
    {
        struct CallArguments
        {
            const std::shared_ptr<const okhttp_websocket_impl> sharedThis;

            const std::string message;
            WebSocketCompletionResult completionResult{ 0 };
        };
        std::unique_ptr<CallArguments> context = std::make_unique<CallArguments>(CallArguments{ shared_from_this(), message });

        return XAsyncBegin(asyncBlock, context.release(), (const void*)HCWebSocketSendMessageAsync, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data)
        {
            CallArguments* context = static_cast<CallArguments *>(data->context);
            auto& sharedThis = context->sharedThis;

            switch (op)
            {
                case XAsyncOp::Begin:
                    return XAsyncSchedule(data->async, 0);
                case XAsyncOp::DoWork:
                {
                    auto lock = sharedThis->Lock();

                    context->completionResult.websocket = sharedThis->GetHandle();
                    context->completionResult.errorCode = sharedThis->SendMessage(sharedThis->m_socketState, context->message);
                    XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                    return S_OK;
                }
                case XAsyncOp::GetResult:
                {
                    if (data->bufferSize < sizeof(WebSocketCompletionResult))
                    {
                        return E_UNEXPECTED;
                    }

                    WebSocketCompletionResult& asyncResult = *static_cast<WebSocketCompletionResult*>(data->buffer);
                    asyncResult = context->completionResult;
                    return S_OK;
                }
                case XAsyncOp::Cleanup:
                {
                    // re-capture previously allocated memory, and free
                    std::unique_ptr<CallArguments> contextPtr{context};
                }
                default:
                    return S_OK;
            }
        });
    }

    HRESULT SendBinaryMessageAsync(const uint8_t* payload, uint32_t payloadSize, XAsyncBlock* asyncBlock) const
    {
        struct CallArguments
        {
            const std::shared_ptr<const okhttp_websocket_impl> sharedThis;

            const std::vector<uint8_t> payload;
            const uint32_t payloadSize;
            WebSocketCompletionResult operationResult{ 0 };
        };

        std::unique_ptr<CallArguments> context = std::make_unique<CallArguments>(
            CallArguments
            {
                shared_from_this(),
                std::vector<uint8_t>(payload, payload + payloadSize),
                payloadSize
            });

        // We use `HCWebSocketSendMessageAsync` instead of `HCWebSocketSendBinaryMessageAsync`
        // as the XAsyncBlock identity due to the identity in `XAsyncGetResult`in
        // `HCGetWebSocketSendMessageResult`, which is used for both binary and non-binary messages.
        // Using the same identity in both will prevent Call/Result mismatches.
        // This mirrors `WinHttpConnection::WebSocketSendMessageAsync`.
        RETURN_IF_FAILED(XAsyncBegin(
            asyncBlock, context.release(),
            (const void*)HCWebSocketSendMessageAsync,
            "HCWebSocketSendMessageAsync",
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            CallArguments* context = static_cast<CallArguments*>(data->context);
            auto& sharedThis = context->sharedThis;

            switch (op)
            {
                case XAsyncOp::Begin:
                    return XAsyncSchedule(data->async, 0);
                case XAsyncOp::DoWork:
                {
                    auto lock = sharedThis->Lock();

                    context->operationResult.websocket = sharedThis->GetHandle();
                    context->operationResult.errorCode = sharedThis->SendBinaryMessage(
                        sharedThis->m_socketState,
                        context->payload.data(),
                        context->payloadSize
                    );
                    XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                    return S_OK;
                }
                case XAsyncOp::GetResult:
                {
                    if (data->bufferSize < sizeof(WebSocketCompletionResult))
                    {
                        return E_UNEXPECTED;
                    }

                    WebSocketCompletionResult& asyncResult = *static_cast<WebSocketCompletionResult*>(data->buffer);
                    asyncResult = context->operationResult;
                    return S_OK;
                }
                case XAsyncOp::Cleanup:
                {
                    // re-capture allocated memory, and free
                    std::unique_ptr<CallArguments> contextPtr{ context };
                    return S_OK;
                }
                default:
                    return S_OK;
            }
        }));

        return S_OK;
    }

    HRESULT DisconnectAsync(HCWebSocketCloseStatus closeStatus) const
    {
        auto asyncBlock = std::make_unique<XAsyncBlock>();
        asyncBlock->queue = nullptr;
        asyncBlock->context = nullptr;
        asyncBlock->callback = [](XAsyncBlock* rawAsyncBlock)
        {
            // re-capture previously allocated memory, and free
            std::unique_ptr<XAsyncBlock> asyncBlock(rawAsyncBlock);
        };

        struct CallArguments
        {
            const std::shared_ptr<const okhttp_websocket_impl> sharedThis;
            const HCWebSocketCloseStatus closeStatus;
        };
        auto context = std::make_unique<CallArguments>(CallArguments{ shared_from_this(), closeStatus });

        HRESULT hr = XAsyncBegin(asyncBlock.get(), context.release(), (const void*)HCWebSocketDisconnect, __FUNCTION__, [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto* context = static_cast<CallArguments*>(data->context);
            auto& sharedThis = context->sharedThis;

            switch (op)
            {
                case XAsyncOp::Begin:
                    return XAsyncSchedule(data->async, 0);
                case XAsyncOp::DoWork:
                {
                    auto lock = sharedThis->Lock();
                    HRESULT hr = sharedThis->Disconnect(sharedThis->m_socketState, context->closeStatus);
                    XAsyncComplete(data->async, hr, 0);
                    return S_OK;
                }
                case XAsyncOp::Cleanup:
                {
                    // re-capture previously allocated memory, and free
                    std::unique_ptr<CallArguments> contextPtr(context);
                    return S_OK;
                }
                default:
                    return S_OK;
            }
        });

        if (SUCCEEDED(hr))
        {
            // release asyncBlock, will be re-captured in callback
            asyncBlock.release();
        }

        return hr;
    }

private:
    using UniqueLock = std::unique_lock<std::mutex>;

    enum class State
    {
        Disconnected, // set by OnClosed
        Connecting, // set by ConnectAsync
        ConnectSucceeded, // set by OnConnectSucceeded
        ConnectFailed, // set by OnConnectFailed
        Connected // set by ConnectAsync
    };

private:
    HRESULT Connect(State currentState, const std::string& uri, const std::string& subProtocol) const
    {
        if (currentState != State::Disconnected)
        {
            return E_HC_CONNECT_ALREADY_CALLED;
        }

        uint32_t headerCount = 0;
        HRESULT hr = HCWebSocketGetNumHeaders(m_handle, &headerCount);
        if (FAILED(hr))
        {
            return hr;
        }

        for (uint32_t i = 0; i < headerCount; ++i)
        {
            const char* name = nullptr;
            const char* value = nullptr;
            hr = HCWebSocketGetHeaderAtIndex(m_handle, i, &name, &value);
            if (FAILED(hr))
            {
                return hr;
            }

            hr = m_javaWebSocket.AddHeader(name, value);
            if (FAILED(hr))
            {
                return hr;
            }
        }

        return m_javaWebSocket.Connect(uri, subProtocol);
    }

    HRESULT SendMessage(State currentState, const std::string& message) const
    {
        if (currentState != State::Connected)
        {
            return E_ILLEGAL_METHOD_CALL;
        }

        return m_javaWebSocket.SendMessage(message);
    }

    HRESULT SendBinaryMessage(State currentState, const uint8_t* data, uint32_t dataSize) const
    {
        if (currentState != State::Connected)
        {
            return E_ILLEGAL_METHOD_CALL;
        }

        return m_javaWebSocket.SendBinaryMessage(data, dataSize);
    }

    HRESULT Disconnect(State currentState, HCWebSocketCloseStatus status) const
    {
        if (currentState != State::Connected)
        {
            return E_ILLEGAL_METHOD_CALL;
        }

        return m_javaWebSocket.Disconnect(static_cast<int>(status));
    }

    void OnOpen(UniqueLock lock)
    {
        if (m_socketState == State::Connecting)
        {
            m_socketState = State::ConnectSucceeded;
        }
    }

    void OnFailure(UniqueLock lock)
    {
        switch (m_socketState)
        {
            case State::Connecting:
                m_socketState = State::ConnectFailed;
                break;
            case State::Connected:
                OnClose(std::move(lock), HCWebSocketCloseStatus::AbnormalClose);
                break;
            default:
                break;
        }
    }

    void OnClose(UniqueLock lock, HCWebSocketCloseStatus status)
    {
        bool invokeCloseCallback = true;
        {
            UniqueLock scoped = std::move(lock);
            switch (m_socketState)
            {
                // some versions of OkHttp call OnClose rather than OnFailure when the websocket
                // fails to connect, handle that case here
                case State::Connecting:
                    m_socketState = State::ConnectFailed;
                    invokeCloseCallback = false;
                    break;
                default:
                    m_socketState = State::Disconnected;
                    break;
            }
        }

        if (invokeCloseCallback)
        {
            HCWebSocketCloseEventFunction closeFunc = nullptr;
            void *context = nullptr;
            if (FAILED(HCWebSocketGetEventFunctions(GetHandle(), nullptr, nullptr, &closeFunc, &context)) || !closeFunc) {
                return;
            }

            closeFunc(GetHandle(), status, context);
        }
    }

    void OnMessage(const std::string& message) const
    {
        HCWebSocketMessageFunction messageFunc = nullptr;
        void* context = nullptr;
        if (FAILED(HCWebSocketGetEventFunctions(GetHandle(), &messageFunc, nullptr, nullptr, &context)) || !messageFunc)
        {
            return;
        }

        messageFunc(GetHandle(), message.c_str(), context);
    }

    void OnBinaryMessage(const uint8_t* data, uint32_t dataSize) const
    {
        HCWebSocketBinaryMessageFunction binaryMessageFunc = nullptr;
        void* context = nullptr;
        if (FAILED(HCWebSocketGetEventFunctions(GetHandle(), nullptr, &binaryMessageFunc, nullptr, &context)) || !binaryMessageFunc)
        {
            return;
        }

        binaryMessageFunc(GetHandle(), data, dataSize, context);
    }

    HCWebsocketHandle GetHandle() const
    {
        return m_handle;
    }

    UniqueLock Lock() const
    {
        return std::unique_lock<std::mutex>{ m_socketMutex };
    }
private:
    State m_socketState = State::Disconnected;
    mutable std::mutex m_socketMutex;

    const HCWebsocketHandle m_handle;
    const HttpClientWebSocket m_javaWebSocket;
};

HRESULT CALLBACK OkHttpWebSocketConnectAsync(
        _In_z_ const char* uri,
        _In_z_ const char* subProtocol,
        _In_ HCWebsocketHandle websocket,
        _Inout_ XAsyncBlock* asyncBlock,
        _In_opt_ void* context,
        _In_ HCPerformEnv env
)
{
    if (!uri || !subProtocol || !websocket || !asyncBlock)
    {
        return E_INVALIDARG;
    }

    auto impl = std::dynamic_pointer_cast<okhttp_websocket_impl>(websocket->websocket->impl);
    if (!impl)
    {
        websocket->websocket->impl = (impl = std::make_shared<okhttp_websocket_impl>(
                env->androidPlatformContext->GetJavaVm(),
                env->androidPlatformContext->GetWebSocketClass(),
                websocket));
    }

    return impl->ConnectAsync(uri, subProtocol, asyncBlock);
}

HRESULT CALLBACK OkHttpWebSocketSendMessageAsync(
        _In_ HCWebsocketHandle websocket,
        _In_z_ const char* message,
        _Inout_ XAsyncBlock* asyncBlock,
        _In_opt_ void* context
)
{
    if (!websocket || !message || !asyncBlock)
    {
        return E_INVALIDARG;
    }

    auto impl = std::dynamic_pointer_cast<okhttp_websocket_impl>(websocket->websocket->impl);
    if (!impl)
    {
        return E_UNEXPECTED;
    }

    return impl->SendMessageAsync(message, asyncBlock);
}

HRESULT CALLBACK OkHttpWebSocketSendBinaryMessageAsync(
        _In_ HCWebsocketHandle websocket,
        _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
        _In_ uint32_t payloadSize,
        _Inout_ XAsyncBlock* asyncBlock,
        _In_opt_ void* context
)
{
    if (!websocket || !payloadBytes || !asyncBlock)
    {
        return E_INVALIDARG;
    }

    auto impl = std::dynamic_pointer_cast<okhttp_websocket_impl>(websocket->websocket->impl);
    if (!impl)
    {
        return E_UNEXPECTED;
    }

    return impl->SendBinaryMessageAsync(payloadBytes, payloadSize, asyncBlock);
}

HRESULT CALLBACK OkHttpWebSocketDisconnect(
        _In_ HCWebsocketHandle websocket,
        _In_ HCWebSocketCloseStatus closeStatus,
        _In_opt_ void* context
)
{
    if (!websocket)
    {
        return E_INVALIDARG;
    }

    auto impl = std::dynamic_pointer_cast<okhttp_websocket_impl>(websocket->websocket->impl);
    if (!impl)
    {
        return E_UNEXPECTED;
    }

    return impl->DisconnectAsync(closeStatus);
}

NAMESPACE_XBOX_HTTP_CLIENT_END

extern "C"
{

JNIEXPORT void JNICALL
Java_com_xbox_httpclient_HttpClientWebSocket_onOpen(JNIEnv *env, jobject thiz)
{
    using namespace xbox::httpclient;

    okhttp_websocket_impl* owner = HttpClientWebSocket::GetOwner(env, thiz);
    if (!owner)
    {
        return;
    }

    owner->OnOpen(owner->Lock());
}

JNIEXPORT void JNICALL
Java_com_xbox_httpclient_HttpClientWebSocket_onFailure(JNIEnv *env, jobject thiz)
{
    using namespace xbox::httpclient;

    okhttp_websocket_impl* owner = HttpClientWebSocket::GetOwner(env, thiz);
    if (!owner)
    {
        return;
    }

    owner->OnFailure(owner->Lock());
}

JNIEXPORT void JNICALL
Java_com_xbox_httpclient_HttpClientWebSocket_onClose(JNIEnv *env, jobject thiz, jint code)
{
    using namespace xbox::httpclient;

    okhttp_websocket_impl* owner = HttpClientWebSocket::GetOwner(env, thiz);
    if (!owner)
    {
        return;
    }

    owner->OnClose(owner->Lock(), HCWebSocketCloseStatus(code));
}

JNIEXPORT void JNICALL
Java_com_xbox_httpclient_HttpClientWebSocket_onMessage(JNIEnv *env, jobject thiz, jstring text)
{
    using namespace xbox::httpclient;

    okhttp_websocket_impl* owner = HttpClientWebSocket::GetOwner(env, thiz);
    if (!owner)
    {
        return;
    }

    const char* message = env->GetStringUTFChars(text, 0);
    if (!message)
    {
        return;
    }

    const jsize messageLength = env->GetStringUTFLength(text);
    if (messageLength <= 0)
    {
        return;
    }

    owner->OnMessage(std::string(message, messageLength));
    env->ReleaseStringUTFChars(text, message);
}

JNIEXPORT void JNICALL
Java_com_xbox_httpclient_HttpClientWebSocket_onBinaryMessage(JNIEnv *env, jobject thiz, jobject data)
{
    using namespace xbox::httpclient;

    okhttp_websocket_impl* owner = HttpClientWebSocket::GetOwner(env, thiz);
    if (!owner)
    {
        return;
    }

    const jlong bufferSize = env->GetDirectBufferCapacity(data);
    if (bufferSize <= 0)
    {
        return;
    }

    const void* const buffer = env->GetDirectBufferAddress(data);
    if (!buffer)
    {
        return;
    }

    owner->OnBinaryMessage(static_cast<const uint8_t*>(buffer), static_cast<uint32_t>(bufferSize));
}

}

#endif // !HC_NOWEBSOCKETS
