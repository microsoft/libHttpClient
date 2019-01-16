// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#if HC_WINHTTP_WEBSOCKETS
#if !HC_NOWEBSOCKETS

#include "../hcwebsocket.h"
#include "../../HTTP/WinHttp/winhttp_http_task.h"
#include "../../global/global.h"
#include "uri.h"

using namespace xbox::httpclient;


struct winhttp_websocket_impl : public hc_websocket_impl, public std::enable_shared_from_this<winhttp_websocket_impl>
{
public:
    winhttp_websocket_impl(HCWebsocketHandle hcHandle)
    {
        m_hcWebsocketHandle = HCWebSocketDuplicateHandle(hcHandle);
        // TODO: when to close
    }

    ~winhttp_websocket_impl()
    {
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
            std::lock_guard<std::mutex> lock(m_outgoingMessageQueueLock);
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
            std::lock_guard<std::mutex> lock(m_httpClientLock);

            message->hr = m_httpTask->send_websocket_message(message->payload.data(), message->payload.length());
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
            std::lock_guard<std::mutex> lock(m_outgoingMessageQueueLock);
            ASSERT(!m_outgoingMessageQueue.empty());
            sendContext->message = std::move(m_outgoingMessageQueue.front());
            m_outgoingMessageQueue.pop();
        }

        auto hr = XAsyncBegin(sendContext->message.asyncBlock, shared_ptr_cache::store(sendContext), (void*)HCWebSocketSendMessageAsync, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            WebSocketCompletionResult* result;
            auto sendMsgContext = shared_ptr_cache::fetch<send_msg_context>(data->context, op == XAsyncOp::Cleanup);

            switch (op)
            {
                case XAsyncOp::DoWork:
                {
                    return sendMsgContext->pThis->send_msg_do_work(&sendMsgContext->message);
                }

                case XAsyncOp::GetResult:
                {
                    result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                    result->platformErrorCode = sendMsgContext->message.hr;
                    result->errorCode = XAsyncGetStatus(data->async, false);
                    return S_OK;
                }

                default: return S_OK;
            }
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
    std::mutex m_httpClientLock; // Guards access to HTTP socket
    HCWebsocketHandle m_hcWebsocketHandle = nullptr;
    std::mutex m_outgoingMessageQueueLock; // Guards access to m_outgoing_msg_queue
    http_internal_queue<websocket_outgoing_message> m_outgoingMessageQueue; // Queue to order the sends   
    std::atomic<int> m_numSends = 0; // Number of sends in progress and queued up.
};


HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ HCPerformEnv env
    )
{
    assert(env != nullptr);

    websocket->uri = uri;
    websocket->subProtocol = subProtocol;
    auto wsppSocket = http_allocate_shared<winhttp_websocket_impl>(websocket);
    websocket->impl = wsppSocket;

    return wsppSocket->connect_async(websocket, asyncBlock, env->m_hSession, env->m_proxyType);
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
#endif