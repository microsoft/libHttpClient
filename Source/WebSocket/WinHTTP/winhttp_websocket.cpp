// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#if HC_WINHTTP_WEBSOCKETS

#include "../hcwebsocket.h"
#include "../../HTTP/WinHttp/winhttp_http_task.h"
#include "../../global/global.h"
#include "uri.h"

using namespace xbox::httpclient;

struct winhttp_websocket_impl : public hc_websocket_impl, public std::enable_shared_from_this<winhttp_websocket_impl>
{
public:
    winhttp_websocket_impl(uint64_t id) : m_id{ id }
    {
    }

    ~winhttp_websocket_impl()
    {
        HC_TRACE_VERBOSE(WEBSOCKET, "winhttp_websocket_impl dtor");
        if (m_call != nullptr)
        {
            HCHttpCallCloseHandle(m_call);
        }
        shared_ptr_cache::remove(m_httpTask.get());
    }

    HRESULT connect_websocket(
        _In_ HCWebsocketHandle websocket,
        _In_ XAsyncBlock* asyncBlock,
        _In_ HCPerformEnv env,
        _In_ proxy_type proxyType)
    {
        HRESULT hr = HCHttpCallCreate(&m_call);
        if (FAILED(hr))
        {
            return hr;
        }

        HCHttpCallRequestSetUrl(m_call, "GET", websocket->Uri().c_str());

        bool isWebsocket = true;

        // Handle if this handle is already connected or connecting
        if (m_httpTask != nullptr)
        {
            if (m_httpTask->m_socketState == WinHttpWebsockState::Connecting ||
                m_httpTask->m_socketState == WinHttpWebsockState::Connected)
            {
                XAsyncComplete(asyncBlock, S_OK, 0);
                return S_OK;
            }
            else
            {
                shared_ptr_cache::remove(m_httpTask.get());
            }
        }

        m_httpTask = http_allocate_shared<winhttp_http_task>(
            asyncBlock, m_call, env, proxyType, isWebsocket
        );

        m_httpTask->m_socketState = WinHttpWebsockState::Connecting;
        m_httpTask->m_websocketHandle = websocket;
        m_httpTask->m_websocketSendCompleteCallback = [ weakThis = std::weak_ptr<winhttp_websocket_impl>{ shared_from_this() }](HRESULT hr)
        {
            if (auto sharedThis{ weakThis.lock() })
            {
                sharedThis->send_complete(hr);
            }
        };

        auto context = shared_ptr_cache::store<winhttp_http_task>(m_httpTask);

        hr = XAsyncBegin(asyncBlock, context, HCWebSocketConnectAsync, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto httpTask = shared_ptr_cache::fetch<winhttp_http_task>(data->context);
            if (!httpTask)
            {
                return E_HC_NOT_INITIALISED;
            }

            switch (op)
            {
            case XAsyncOp::DoWork:
            {
                HRESULT hr = httpTask->connect_and_send_async();
                if (FAILED(hr))
                {
                    return hr;
                }
                return E_PENDING;
            }
            case XAsyncOp::GetResult:
            {
                auto result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                result->websocket = httpTask->m_websocketHandle;
                result->platformErrorCode = httpTask->m_connectPlatformError;
                result->errorCode = httpTask->m_connectHr;
                return S_OK;
            }
            default: return S_OK;
            }
        });

        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(asyncBlock, 0);
        }
        return hr;
    }

    HRESULT send_websocket_message_async(_In_ XAsyncBlock* asyncBlock, _In_ const char* payloadPtr)
    {
        if (payloadPtr == nullptr)
        {
            return E_INVALIDARG;
        }

        auto httpSingleton = get_http_singleton();
        if (httpSingleton == nullptr)
        {
            return E_HC_NOT_INITIALISED;
        }

        http_internal_string payload(payloadPtr);
        if (payload.length() == 0)
        {
            return E_INVALIDARG;
        }

        auto sendContext = http_allocate_shared<send_msg_context>();
        sendContext->asyncBlock = asyncBlock;
        sendContext->payload = std::move(payload);
        sendContext->id = ++httpSingleton->m_lastId;
        sendContext->pThis = shared_from_this();

        return enqueue_message(std::move(sendContext));
    }

    HRESULT send_websocket_binary_message_async(
        _In_ XAsyncBlock* asyncBlock, 
        _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
        _In_ uint32_t payloadSize)
    {
        if (payloadBytes == nullptr)
        {
            return E_INVALIDARG;
        }

        auto httpSingleton = get_http_singleton();
        if (httpSingleton == nullptr)
        {
            return E_HC_NOT_INITIALISED;
        }

        auto sendContext = http_allocate_shared<send_msg_context>();
        sendContext->asyncBlock = asyncBlock;
        sendContext->binaryPayload.resize(payloadSize);
        memcpy(&sendContext->binaryPayload[0], payloadBytes, payloadSize);
        sendContext->id = ++httpSingleton->m_lastId;
        sendContext->pThis = shared_from_this();
    
        return enqueue_message(sendContext);
    }

    HRESULT disconnect_websocket(_In_ HCWebSocketCloseStatus closeStatus)
    {
        assert(m_httpTask);
        HRESULT hr = S_OK;
        if (m_httpTask)
        {
            hr = m_httpTask->disconnect_websocket(closeStatus);
        }
        return hr;
    }

private:
    struct send_msg_context
    {
        XAsyncBlock* asyncBlock{};
        http_internal_string payload;
        http_internal_vector<uint8_t> binaryPayload;
        HRESULT hr{ S_OK };
        uint64_t id;
        std::shared_ptr<winhttp_websocket_impl> pThis;
    };

    HRESULT enqueue_message(std::shared_ptr<send_msg_context> sendContext)
    {
        auto rawSendContext = shared_ptr_cache::store(sendContext);
        if (rawSendContext == nullptr)
        {
            XAsyncComplete(sendContext->asyncBlock, E_HC_NOT_INITIALISED, 0);
            return E_HC_NOT_INITIALISED;
        }

        return XAsyncBegin(sendContext->asyncBlock, rawSendContext, (void*)HCWebSocketSendMessageAsync, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto context = shared_ptr_cache::fetch<send_msg_context>(data->context);
            if (context == nullptr)
            {
                return E_HC_NOT_INITIALISED;
            }

            WebSocketCompletionResult* result;
            switch (op)
            {
                case XAsyncOp::Begin:
                try
                {
                    // By design, limit to a single WinHttp send at a time. If there isn't another send already in progress,
                    // send the message now. winhttp_http_task::send_websocket_message is asynchronous; the winhttp_http_task::m_sendCompleteCallback
                    // will be invoked when the send has completed.
                    // If there is already a send in progress, return E_PENDING and wait for that to complete before sending this message.

                    std::unique_lock<std::recursive_mutex> lock{ context->pThis->m_mutex };
                    if (context->pThis->m_outgoingMessage == nullptr)
                    {
                        context->pThis->m_outgoingMessage = context;
                        lock.unlock();

                        context->pThis->send_message(context);
                    }
                    else
                    {
                        context->pThis->m_pendingMessages.push(context);
                    }
                    return E_PENDING;
                }
                catch (...)
                {
                    HC_TRACE_ERROR(WEBSOCKET, "[WinHttp][ID %llu] Caught exception sending message[ID %llu]", TO_ULL(context->pThis->m_id), TO_ULL(context->id));
                    return E_FAIL;
                }

                case XAsyncOp::GetResult:
                {
                    result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                    result->platformErrorCode = context->hr;
                    result->errorCode = SUCCEEDED(context->hr) ? S_OK : E_FAIL;
                    return S_OK;
                }

                case XAsyncOp::Cleanup:
                {
                    shared_ptr_cache::remove(data->context);
                    return S_OK;
                }

                default: return S_OK;
            }
        });
    }

    void send_complete(HRESULT result)
    {
        std::unique_lock<std::recursive_mutex> lock{ m_mutex };
        ASSERT(m_outgoingMessage != nullptr);
        m_outgoingMessage->hr = result;
        XAsyncComplete(m_outgoingMessage->asyncBlock, result, sizeof(WebSocketCompletionResult));
        m_outgoingMessage.reset();

        // Send next message in queue
        if (!m_pendingMessages.empty())
        {
            m_outgoingMessage = m_pendingMessages.front();
            m_pendingMessages.pop();
            lock.unlock();

            send_message(m_outgoingMessage);
        }
    }

    void send_message(std::shared_ptr<send_msg_context> context)
    {
        HC_TRACE_VERBOSE(WEBSOCKET, "[WinHttp][ID %llu] sending message[ID %llu]...", TO_ULL(m_id), TO_ULL(context->id));

        if (context->payload.length() > 0)
        {
            m_httpTask->send_websocket_message(WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, reinterpret_cast<const void*>(context->payload.data()), context->payload.length());
        }
        else
        {
            m_httpTask->send_websocket_message(WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, reinterpret_cast<const void*>(context->binaryPayload.data()), context->binaryPayload.size());
        }
    }

    std::shared_ptr<xbox::httpclient::winhttp_http_task> m_httpTask;
    HCCallHandle m_call = nullptr;

    // Websocket state
    const uint64_t m_id;
    http_internal_queue<std::shared_ptr<send_msg_context>> m_pendingMessages; // Queue to order the sends
    std::shared_ptr<send_msg_context> m_outgoingMessage;

    std::recursive_mutex m_mutex;
};

HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv env
    )
{
    assert(env != nullptr);

    if (uri == nullptr || websocket == nullptr || subProtocol == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<winhttp_websocket_impl> impl{ nullptr };
    if (websocket->impl == nullptr)
    {
        impl = http_allocate_shared<winhttp_websocket_impl>(websocket->id);
        websocket->impl = impl;
    }
    else
    {
        impl = std::dynamic_pointer_cast<winhttp_websocket_impl>(websocket->impl);
    }

    return impl->connect_websocket(websocket, asyncBlock, env, env->m_proxyType);
}

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* async,
    _In_opt_ void* /*context*/
    )
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<winhttp_websocket_impl> httpSocket = std::dynamic_pointer_cast<winhttp_websocket_impl>(websocket->impl);
    if (httpSocket == nullptr)
    {
        return E_UNEXPECTED;
    }
    return httpSocket->send_websocket_message_async(async, message);
}

HRESULT CALLBACK Internal_HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/)
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<winhttp_websocket_impl> httpSocket = std::dynamic_pointer_cast<winhttp_websocket_impl>(websocket->impl);
    if (httpSocket == nullptr)
    {
        return E_UNEXPECTED;
    }
    return httpSocket->send_websocket_binary_message_async(asyncBlock, payloadBytes, payloadSize);
}

HRESULT CALLBACK Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* /*context*/
    )
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }
   
    std::shared_ptr<winhttp_websocket_impl> httpSocket = std::dynamic_pointer_cast<winhttp_websocket_impl>(websocket->impl);
    if (httpSocket == nullptr)
    {
        return E_UNEXPECTED;
    }
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", TO_ULL(websocket->id));
    return httpSocket->disconnect_websocket(closeStatus);
}

#endif
