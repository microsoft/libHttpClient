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
    ~winhttp_websocket_impl()
    {
        HC_TRACE_VERBOSE(WEBSOCKET, "winhttp_websocket_impl dtor");
        if(m_call != nullptr) HCHttpCallCloseHandle(m_call);
        shared_ptr_cache::remove<winhttp_http_task>(m_httpTask.get());
    }

    HRESULT connect_async(
        _In_ HCWebsocketHandle websocket,
        _In_ XAsyncBlock* asyncBlock,
        _In_ HINTERNET hSession,
        _In_ proxy_type proxyType)
    {
        HRESULT hr = HCHttpCallCreate(&m_call);
        if (FAILED(hr))
        {
            return hr;
        }

        HCHttpCallRequestSetUrl(m_call, "GET", websocket->uri.c_str());

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
        }

        m_httpTask = http_allocate_shared<winhttp_http_task>(
            asyncBlock, m_call, hSession, proxyType, isWebsocket
            );

        m_httpTask->m_socketState = WinHttpWebsockState::Connecting;
        m_httpTask->m_websocketHandle = websocket;

        shared_ptr_cache::store<winhttp_http_task>(m_httpTask);
        return m_httpTask->connect_and_send_async();
    }

    HRESULT send_websocket_message_async(_In_ XAsyncBlock* asyncBlock, _In_ const char* payloadPtr)
    {
        if (payloadPtr == nullptr)
        {
            return E_INVALIDARG;
        }

        auto httpSingleton = get_http_singleton(false);
        if (httpSingleton == nullptr)
        {
            return E_HC_NOT_INITIALISED;
        }

        http_internal_string payload(payloadPtr);
        if (payload.length() == 0)
        {
            return E_INVALIDARG;
        }

        websocket_outgoing_message message;
        message.asyncBlock = asyncBlock;
        message.payload = std::move(payload);
        message.id = ++httpSingleton->m_lastId;

        {
            // Only actually have to take the lock if touching the queue.
            std::lock_guard<std::recursive_mutex> lock(m_outgoingMessageQueueLock);
            m_outgoingMessageQueue.push(message);
        }

        if (++m_numSends == 1) // No sends in progress
        {
            // Start sending the message
            return send_msg();
        }

        return S_OK;
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

        auto httpSingleton = get_http_singleton(false);
        if (httpSingleton == nullptr)
        {
            return E_HC_NOT_INITIALISED;
        }

        websocket_outgoing_message message;
        message.asyncBlock = asyncBlock;
        message.binaryPayload.resize(payloadSize);
        memcpy(&message.binaryPayload[0], payloadBytes, payloadSize);
        message.id = ++httpSingleton->m_lastId;

        {
            // Only actually have to take the lock if touching the queue.
            std::lock_guard<std::recursive_mutex> lock(m_outgoingMessageQueueLock);
            m_outgoingMessageQueue.push(message);
        }

        if (++m_numSends == 1) // No sends in progress
        {
            // Start sending the message
            return send_msg();
        }

        return S_OK;
    }

    struct websocket_outgoing_message
    {
        XAsyncBlock* asyncBlock = nullptr;
        http_internal_string payload;
        http_internal_vector<uint8_t> binaryPayload;
        HRESULT hr = S_OK;
        uint64_t id = 0;
    };

    struct send_msg_context
    {
        std::shared_ptr<winhttp_websocket_impl> pThis;
        websocket_outgoing_message message;
    };

    HRESULT send_msg_do_work(websocket_outgoing_message* message)
    {
        HRESULT hr = S_OK;

        try
        {
            std::lock_guard<std::recursive_mutex> lock(m_httpClientLock);

            if(message->payload.length() > 0)
                message->hr = m_httpTask->send_websocket_message(WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, reinterpret_cast<const void*>(message->payload.data()), message->payload.length());
            else
                message->hr = m_httpTask->send_websocket_message(WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, reinterpret_cast<const void*>(message->binaryPayload.data()), message->binaryPayload.size());
            XAsyncComplete(message->asyncBlock, message->hr, sizeof(WebSocketCompletionResult));

            if (--m_numSends > 0)
            {
                hr = send_msg();
            }
        }
        catch (...)
        {
            hr = E_FAIL;
            XAsyncComplete(message->asyncBlock, hr, sizeof(WebSocketCompletionResult));
        }
        return hr;
    }

    // Pull the next message from the queue and send it
    HRESULT send_msg()
    {
        auto sendContext = http_allocate_shared<send_msg_context>();
        sendContext->pThis = shared_from_this();
        {
            std::lock_guard<std::recursive_mutex> lock(m_outgoingMessageQueueLock);
            ASSERT(!m_outgoingMessageQueue.empty());
            sendContext->message = std::move(m_outgoingMessageQueue.front());
            m_outgoingMessageQueue.pop();
        }

        auto hr = XAsyncBegin(sendContext->message.asyncBlock, shared_ptr_cache::store(sendContext), (void*)HCWebSocketSendMessageAsync, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            WebSocketCompletionResult* result;
            switch (op)
            {
                case XAsyncOp::DoWork:
                {
                    auto sendMsgContext = shared_ptr_cache::fetch<send_msg_context>(data->context, true);
                    return sendMsgContext->pThis->send_msg_do_work(&sendMsgContext->message);
                }

                case XAsyncOp::GetResult:
                {
                    auto sendMsgContext = shared_ptr_cache::fetch<send_msg_context>(data->context, true);
                    result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                    result->platformErrorCode = sendMsgContext->message.hr;
                    result->errorCode = XAsyncGetStatus(data->async, false);
                    return S_OK;
                }

                case XAsyncOp::Cleanup:
                {
                    shared_ptr_cache::remove<send_msg_context>(data->context);
                    return S_OK;
                }
            }

            return S_OK;
        });

        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(sendContext->message.asyncBlock, 0);
        }
        return hr;
    }


    HRESULT disconnect_websocket(_In_ HCWebSocketCloseStatus closeStatus)
    {
        return m_httpTask->disconnect_websocket(closeStatus);
    }

    std::shared_ptr<xbox::httpclient::winhttp_http_task> m_httpTask;
    HCCallHandle m_call = nullptr;

    // Websocket state
    std::recursive_mutex m_httpClientLock; // Guards access to HTTP socket
    HCWebsocketHandle m_hcWebsocketHandle = nullptr;
    std::recursive_mutex m_outgoingMessageQueueLock; // Guards access to m_outgoing_msg_queue
    http_internal_queue<websocket_outgoing_message> m_outgoingMessageQueue; // Queue to order the sends   
    std::atomic<int> m_numSends = 0; // Number of sends in progress and queued up.
};


typedef struct websocket_connect_context
{
    HCWebsocketHandle websocket;
    XAsyncBlock* outerAsyncBlock;
    HCPerformEnv env;
} websocket_connect_context;

HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ HCPerformEnv env
    )
{
    assert(env != nullptr);

    if (uri == nullptr || websocket == nullptr || subProtocol == nullptr)
    {
        return E_INVALIDARG;
    }

    if (websocket->impl == nullptr)
    {
        auto httpSocket = http_allocate_shared<winhttp_websocket_impl>();
        websocket->uri = uri;
        websocket->subProtocol = subProtocol;
        websocket->impl = httpSocket;
    }

    std::shared_ptr<websocket_connect_context> connectContext = std::make_shared<websocket_connect_context>();
    connectContext->websocket = websocket;
    connectContext->outerAsyncBlock = asyncBlock;
    connectContext->env = env;
    auto storedPtr = shared_ptr_cache::store<websocket_connect_context>(connectContext);
    websocket_connect_context* rawConnectContext = static_cast<websocket_connect_context*>(storedPtr);

    HRESULT hr = XAsyncBegin(asyncBlock, rawConnectContext, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](_In_ XAsyncOp op, _In_ const XAsyncProviderData* data)
    {
        switch (op)
        {
            case XAsyncOp::DoWork:
            {
                websocket_connect_context* rawConnectContext = static_cast<websocket_connect_context*>(data->context);
                auto httpSingleton = get_http_singleton(true);
                if (nullptr == httpSingleton)
                    return E_HC_NOT_INITIALISED;

                auto connectFunc = httpSingleton->m_websocketConnectFunc;
                HRESULT hr = S_OK;
                if (connectFunc != nullptr)
                {
                    try
                    {
                        rawConnectContext->websocket->connectCalled = true;
                        std::shared_ptr<winhttp_websocket_impl> winhttpSocket = std::dynamic_pointer_cast<winhttp_websocket_impl>(rawConnectContext->websocket->impl);
                        hr = winhttpSocket->connect_async(
                            rawConnectContext->websocket, 
                            rawConnectContext->outerAsyncBlock,
                            rawConnectContext->env->m_hSession,
                            rawConnectContext->env->m_proxyType);
                        if (FAILED(hr))
                        {
                            return hr;
                        }
                    }
                    catch (...)
                    {
                        HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketConnect [ID %llu]: failed", rawConnectContext->websocket->id);
                    }
                }
                return E_PENDING;
            }

            case XAsyncOp::Cleanup:
            {
                shared_ptr_cache::remove<websocket_connect_context>(data->context);
                break;
            }
        }

        return S_OK;
    });

    if (hr == S_OK)
    {
        hr = XAsyncSchedule(asyncBlock, 0);
    }

    return hr;
}

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* async
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
    _Inout_ XAsyncBlock* asyncBlock)
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
    _In_ HCWebSocketCloseStatus closeStatus
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
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", websocket->id);
    return httpSocket->disconnect_websocket(closeStatus);
}

#endif
