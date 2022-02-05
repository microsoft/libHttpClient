// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "winrt_websocket.h"

using namespace xbox::httpclient;
using namespace ::Windows::Foundation;
using namespace ::Windows::Storage;
using namespace ::Windows::Storage::Streams;
using namespace ::Windows::Networking;
using namespace ::Windows::Networking::Sockets;


class websocket_outgoing_message
{
public: 
    http_internal_string m_message;
    http_internal_vector<uint8_t> m_messageBinary;
    XAsyncBlock* m_asyncBlock;
    DataWriterStoreOperation^ m_storeAsyncOp;
    AsyncStatus m_storeAsyncOpStatus;
    HRESULT m_storeAsyncResult;
    uint64_t m_id;
};

// This class is required by the implementation in order to function:
// The TypedEventHandler requires the message received and close handler to be a member of WinRT class.
ref class ReceiveContext sealed
{
public:
    ReceiveContext() : m_webocketHandle(nullptr)
    {
    }

    friend HRESULT WebsocketConnectDoWork(
        _Inout_ XAsyncBlock* asyncBlock,
        _In_opt_ void* executionRoutineContext
        );

    void OnReceive(MessageWebSocket^ sender, MessageWebSocketMessageReceivedEventArgs^ args);
    void OnClosed(IWebSocket^ sender, WebSocketClosedEventArgs^ args);

private:
    HCWebsocketHandle m_webocketHandle;
};

class winrt_websocket_impl : public hc_websocket_impl
{
public:
    winrt_websocket_impl() : m_connectAsyncOpResult(S_OK)
    {
        m_outgoingMessageSendInProgress = false;
        m_websocketHandle = nullptr;
    }

    Windows::Networking::Sockets::MessageWebSocket^ m_messageWebSocket;
    bool m_connectClosing{ false };
    Windows::Storage::Streams::DataWriter^ m_messageDataWriter;
    HRESULT m_connectAsyncOpResult;
    ReceiveContext^ m_context;

    IAsyncAction^ m_connectAsyncOp;

    std::recursive_mutex m_outgoingMessageQueueLock;
    std::queue<std::shared_ptr<websocket_outgoing_message>> m_outgoingMessageQueue;
    HCWebsocketHandle m_websocketHandle;
    std::atomic<bool> m_outgoingMessageSendInProgress;
};

void MessageWebSocketSendMessage(
    _In_ std::shared_ptr<winrt_websocket_impl> websocketTask
    );

void ReceiveContext::OnReceive(MessageWebSocket^ sender, MessageWebSocketMessageReceivedEventArgs^ args)
{
    try
    {
        DataReader^ reader;

        try
        {
            reader = args->GetDataReader();
        }
        catch (Platform::Exception ^e)
        {
            // Close websocket on this error
            // https://docs.microsoft.com/en-us/uwp/api/windows.networking.sockets.messagewebsocket.messagereceived
            sender->Close(static_cast<unsigned short>(HCWebSocketCloseStatus::UnknownError), nullptr);
            return;
        }

        const auto len = reader->UnconsumedBufferLength;
        if (len > 0)
        {
            std::string payload;
            payload.resize(len);
            reader->ReadBytes(Platform::ArrayReference<uint8_t>(reinterpret_cast<uint8 *>(&payload[0]), len));
            HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: receieved msg [%s]", TO_ULL(m_webocketHandle->websocket->id), payload.c_str());

            HCWebSocketMessageFunction messageFunc = nullptr;
            HCWebSocketBinaryMessageFunction binaryMessageFunc = nullptr;
            void* context = nullptr;
            HCWebSocketGetEventFunctions(m_webocketHandle, &messageFunc, &binaryMessageFunc, nullptr, &context);

            if (args->MessageType == SocketMessageType::Utf8)
            {
                if (messageFunc != nullptr)
                {
                    messageFunc(m_webocketHandle, payload.c_str(), context);
                }
            }
            else if (args->MessageType == SocketMessageType::Binary)
            {
                if (binaryMessageFunc != nullptr)
                {
                    binaryMessageFunc(m_webocketHandle, (uint8_t*)payload.c_str(), (uint32_t)payload.size(), context);
                }
            }
        }
    }
    catch (Platform::Exception ^e)
    {
    }
    catch (...)
    {
    }
}

void ReceiveContext::OnClosed(IWebSocket^ sender, WebSocketClosedEventArgs^ args)
{
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: on closed event triggered", TO_ULL(m_webocketHandle->websocket->id));

    std::shared_ptr<winrt_websocket_impl> websocketTask = std::dynamic_pointer_cast<winrt_websocket_impl>(m_webocketHandle->websocket->impl);
    if (websocketTask->m_connectClosing)
    {
        return;
    }

    HCWebSocketCloseEventFunction closeFunc = nullptr;
    void* context = nullptr;
    HCWebSocketGetEventFunctions(m_webocketHandle, nullptr, nullptr, &closeFunc, &context);
    if (closeFunc != nullptr)
    {
        closeFunc(m_webocketHandle, static_cast<HCWebSocketCloseStatus>(args->Code), context);
    }
}

inline bool str_icmp(const char* left, const char* right)
{
#ifdef _WIN32
    return _stricmp(left, right) == 0;
#else
    return boost::iequals(left, right);
#endif
}

http_internal_vector<http_internal_wstring> parse_subprotocols(const http_internal_string& subProtocol)
{
    http_internal_vector<http_internal_wstring> values;
    http_internal_wstring token;
    std::wstringstream header(utf16_from_utf8(subProtocol).c_str());

    while (std::getline(header, token, L','))
    {
        trim_whitespace(token);
        if (!token.empty())
        {
            values.push_back(token);
        }
    }

    return values;
}

HRESULT WebsocketConnectDoWork(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* executionRoutineContext
    )
try
{
    HCWebsocketHandle websocketHandle = static_cast<HCWebsocketHandle>(executionRoutineContext);
    auto websocket = websocketHandle->websocket;
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Connect executing", TO_ULL(websocket->id));
    std::shared_ptr<winrt_websocket_impl> websocketTask = std::dynamic_pointer_cast<winrt_websocket_impl>(websocket->impl);

    try
    {
        websocketTask->m_connectClosing = false;
        websocketTask->m_messageWebSocket = ref new MessageWebSocket();

        uint32_t numHeaders = 0;
        HCWebSocketGetNumHeaders(websocketHandle, &numHeaders);

        http_internal_string protocolHeader("Sec-WebSocket-Protocol");
        for (uint32_t i = 0; i < numHeaders; i++)
        {
            const char* headerName;
            const char* headerValue;
            HCWebSocketGetHeaderAtIndex(websocketHandle, i, &headerName, &headerValue);

            // The MessageWebSocket API throws a COMException if you try to set the
            // 'Sec-WebSocket-Protocol' header here. It requires you to go through their API instead.
            if (headerName != nullptr && headerValue != nullptr && !str_icmp(headerName, protocolHeader.c_str()))
            {
                http_internal_wstring wHeaderName = utf16_from_utf8(headerName);
                http_internal_wstring wHeaderValue = utf16_from_utf8(headerValue);
                websocketTask->m_messageWebSocket->SetRequestHeader(
                    Platform::StringReference(wHeaderName.c_str()),
                    Platform::StringReference(wHeaderValue.c_str()));
                HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Header %d [%s: %s]", TO_ULL(websocket->id), i, headerName, headerValue);
            }
        }

        auto protocols = parse_subprotocols(websocket->SubProtocol());
        for (const auto& value : protocols)
        {
            websocketTask->m_messageWebSocket->Control->SupportedProtocols->Append(Platform::StringReference(value.c_str()));
            HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Protocol [%S]", TO_ULL(websocket->id), value.c_str());
        }

        websocketTask->m_context = ref new ReceiveContext();
        websocketTask->m_context->m_webocketHandle = websocketHandle;

        http_internal_wstring aUrl = utf16_from_utf8(websocket->Uri());
        const auto cxUri = ref new Windows::Foundation::Uri(Platform::StringReference(aUrl.c_str()));

        websocketTask->m_messageWebSocket->MessageReceived += ref new TypedEventHandler<MessageWebSocket^, MessageWebSocketMessageReceivedEventArgs^>(websocketTask->m_context, &ReceiveContext::OnReceive);
        websocketTask->m_messageWebSocket->Closed += ref new TypedEventHandler<IWebSocket^, WebSocketClosedEventArgs^>(websocketTask->m_context, &ReceiveContext::OnClosed);

        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: connecting to %s", TO_ULL(websocket->id), websocket->Uri().c_str());

        websocketTask->m_connectAsyncOp = websocketTask->m_messageWebSocket->ConnectAsync(cxUri);

        websocketTask->m_connectAsyncOp->Completed = ref new AsyncActionCompletedHandler(
            [websocket, websocketTask, asyncBlock](
                Windows::Foundation::IAsyncAction^ asyncOp,
                Windows::Foundation::AsyncStatus status)
        {
            UNREFERENCED_PARAMETER(status);
            try
            {
                websocketTask->m_messageDataWriter = ref new DataWriter(websocketTask->m_messageWebSocket->OutputStream);
                if (status == Windows::Foundation::AsyncStatus::Error)
                {
                    websocketTask->m_connectAsyncOpResult = E_FAIL;
                }
                else
                {
                    websocketTask->m_connectAsyncOpResult = S_OK;
                }
            }
            catch (Platform::Exception^ e)
            {
                websocketTask->m_connectAsyncOpResult = e->HResult;
            }
            catch (...)
            {
                websocketTask->m_connectAsyncOpResult = E_FAIL;
            }

            if (FAILED(websocketTask->m_connectAsyncOpResult))
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: connect failed 0x%0.8x", TO_ULL(websocket->id), websocketTask->m_connectAsyncOpResult);

                try
                {
                    // Cleaning up the websocket state
                    websocketTask->m_connectClosing = true;
                    if (websocketTask->m_messageWebSocket != nullptr)
                    {
                        websocketTask->m_messageWebSocket->Close(static_cast<unsigned short>(HCWebSocketCloseStatus::Normal), "");
                        websocketTask->m_messageWebSocket = nullptr;
                    }
                }
                catch (...)
                {
                }
            }
            else
            {
                HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu] connect complete", TO_ULL(websocket->id));
            }

            XAsyncComplete(asyncBlock, S_OK, sizeof(WebSocketCompletionResult));
        });
    }
    catch (Platform::Exception^ e)
    {
        try
        {
            // Cleaning up the websocket state
            websocketTask->m_connectClosing = true;
            if (websocketTask->m_messageWebSocket != nullptr)
            {
                websocketTask->m_messageWebSocket->Close(static_cast<unsigned short>(HCWebSocketCloseStatus::Normal), "");
                websocketTask->m_messageWebSocket = nullptr;
            }
        }
        catch (...)
        {
        }

        HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: ConnectAsync failed = 0x%0.8x", TO_ULL(websocketTask->m_websocketHandle->websocket->id), e->HResult);
        return e->HResult;
    }

    return E_PENDING;
}
CATCH_RETURN()

HRESULT WebsocketConnectGetResult(_In_ const XAsyncProviderData* data)
{
    HCWebsocketHandle websocketHandle = static_cast<HCWebsocketHandle>(data->context);
    std::shared_ptr<winrt_websocket_impl> websocketTask = std::dynamic_pointer_cast<winrt_websocket_impl>(websocketHandle->websocket->impl);

    WebSocketCompletionResult result = {};
    result.websocket = websocketHandle;
    result.errorCode = (FAILED(websocketTask->m_connectAsyncOpResult)) ? E_FAIL : S_OK;
    result.platformErrorCode = websocketTask->m_connectAsyncOpResult;
    CopyMemory(data->buffer, &result, sizeof(WebSocketCompletionResult));

    return S_OK;
}

HRESULT CALLBACK WinRTWebSocketConnectAsync(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HCWebsocketHandle websocketHandle,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env)
{
    UNREFERENCED_PARAMETER(uri);
    UNREFERENCED_PARAMETER(subProtocol);
    UNREFERENCED_PARAMETER(env);
    UNREFERENCED_PARAMETER(context);

    std::shared_ptr<winrt_websocket_impl> websocketTask = http_allocate_shared<winrt_websocket_impl>();
    websocketTask->m_websocketHandle = websocketHandle;
    websocketHandle->websocket->impl = std::dynamic_pointer_cast<hc_websocket_impl>(websocketTask);

    HRESULT hr = XAsyncBegin(asyncBlock, websocketHandle, HCWebSocketConnectAsync, __FUNCTION__,
        [](_In_ XAsyncOp op, _In_ const XAsyncProviderData* data)
    {
        switch (op)
        {
            case XAsyncOp::DoWork: return WebsocketConnectDoWork(data->async, data->context);
            case XAsyncOp::GetResult: return WebsocketConnectGetResult(data);
        }

        return S_OK;
    });

    if (hr == S_OK)
    {
        hr = XAsyncSchedule(asyncBlock, 0);
    }

    return hr;
}

HRESULT CALLBACK WinRTWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocketHandle,
    _In_z_ PCSTR message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(context);
    if (message == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;
    std::shared_ptr<winrt_websocket_impl> websocketTask = std::dynamic_pointer_cast<winrt_websocket_impl>(websocketHandle->websocket->impl);
    if(websocketTask == nullptr)
        return E_HC_NOT_INITIALISED;

    std::shared_ptr<websocket_outgoing_message> msg = http_allocate_shared<websocket_outgoing_message>();
    msg->m_message = message;
    msg->m_asyncBlock = asyncBlock;
    msg->m_id = ++httpSingleton->m_lastId;

    if (msg->m_message.length() == 0)
    {
        return E_INVALIDARG;
    }

    {
        std::lock_guard<std::recursive_mutex> lock(websocketTask->m_outgoingMessageQueueLock);
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: send msg queue size: %zu", TO_ULL(websocketTask->m_websocketHandle->websocket->id), websocketTask->m_outgoingMessageQueue.size());
        websocketTask->m_outgoingMessageQueue.push(msg);
    }

    // No sends in progress, so start sending the message
    bool expectedSendInProgress = false;
    if (websocketTask->m_outgoingMessageSendInProgress.compare_exchange_strong(expectedSendInProgress, true))
    {
        MessageWebSocketSendMessage(websocketTask);
    }
    
    return S_OK;
}

HRESULT CALLBACK WinRTWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocketHandle,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context)
{
    UNREFERENCED_PARAMETER(context);

    if (payloadBytes == nullptr)
    {
        return E_INVALIDARG;
    }

    if (payloadSize == 0)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;
    std::shared_ptr<winrt_websocket_impl> websocketTask = std::dynamic_pointer_cast<winrt_websocket_impl>(websocketHandle->websocket->impl);

    std::shared_ptr<websocket_outgoing_message> msg = http_allocate_shared<websocket_outgoing_message>();
    msg->m_messageBinary.assign(payloadBytes, payloadBytes + payloadSize);
    msg->m_asyncBlock = asyncBlock;
    msg->m_id = ++httpSingleton->m_lastId;

    {
        std::lock_guard<std::recursive_mutex> lock(websocketTask->m_outgoingMessageQueueLock);
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: send msg queue size: %zu", TO_ULL(websocketTask->m_websocketHandle->websocket->id), websocketTask->m_outgoingMessageQueue.size());
        websocketTask->m_outgoingMessageQueue.push(msg);
    }

    // No sends in progress, so start sending the message
    bool expectedSendInProgress = false;
    if (websocketTask->m_outgoingMessageSendInProgress.compare_exchange_strong(expectedSendInProgress, true))
    {
        MessageWebSocketSendMessage(websocketTask);
    }

    return S_OK;
}

struct SendMessageCallbackContext
{
    std::shared_ptr<websocket_outgoing_message> nextMessage;
    std::shared_ptr<winrt_websocket_impl> websocketTask;
};

HRESULT WebsockSendMessageDoWork(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* executionRoutineContext
    )
try
{
    auto sendMsgContext = shared_ptr_cache::fetch<SendMessageCallbackContext>(executionRoutineContext);
    if (sendMsgContext == nullptr)
    {
        return E_HC_NOT_INITIALISED;
    }

    auto websocketTask = sendMsgContext->websocketTask;

    try
    {
        auto websocketHandle = websocketTask->m_websocketHandle;
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Send message executing", TO_ULL(websocketHandle->websocket->id));

        auto msg = sendMsgContext->nextMessage;
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Message [ID %llu] [%s]", TO_ULL(websocketHandle->websocket->id), TO_ULL(msg->m_id), msg->m_message.c_str());

        UNREFERENCED_LOCAL(websocketHandle);

        if (!msg->m_message.empty())
        {
            websocketTask->m_messageWebSocket->Control->MessageType = SocketMessageType::Utf8;
            unsigned char* uchar = reinterpret_cast<unsigned char*>(const_cast<char*>(msg->m_message.c_str()));
            websocketTask->m_messageDataWriter->WriteBytes(Platform::ArrayReference<unsigned char>(uchar, static_cast<unsigned int>(msg->m_message.length())));
        }
        else
        {
            websocketTask->m_messageWebSocket->Control->MessageType = SocketMessageType::Binary;
            websocketTask->m_messageDataWriter->WriteBytes(Platform::ArrayReference<unsigned char>(msg->m_messageBinary.data(), static_cast<unsigned int>(msg->m_messageBinary.size())));
        }


        msg->m_storeAsyncOp = websocketTask->m_messageDataWriter->StoreAsync();

        msg->m_storeAsyncOp->Completed = ref new AsyncOperationCompletedHandler<unsigned int>(
            [websocketTask, msg, asyncBlock](Windows::Foundation::IAsyncOperation<unsigned int>^ asyncOp, Windows::Foundation::AsyncStatus status)
        {
            try
            {
                msg->m_storeAsyncOpStatus = status;
                unsigned int result = asyncOp->GetResults();
                HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Message [ID %llu] send complete = %d", TO_ULL(websocketTask->m_websocketHandle->websocket->id), msg->m_id, result);
                msg->m_storeAsyncResult = result;
            }
            catch (Platform::Exception^ ex)
            {
                msg->m_storeAsyncResult = ex->HResult;
            }
            catch (...)
            {
                msg->m_storeAsyncResult = E_FAIL;
            }

            if (FAILED(msg->m_storeAsyncResult))
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: Message [ID %llu] send failed = 0x%0.8x", TO_ULL(websocketTask->m_websocketHandle->websocket->id), msg->m_id, msg->m_storeAsyncResult);
            }
            XAsyncComplete(asyncBlock, msg->m_storeAsyncResult, sizeof(WebSocketCompletionResult));
            MessageWebSocketSendMessage(websocketTask);
        });
    }
    catch (Platform::Exception^ e)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: Send failed = 0x%0.8x", TO_ULL(websocketTask->m_websocketHandle->websocket->id), e->HResult);
        bool expectedSendInProgress = true;
        websocketTask->m_outgoingMessageSendInProgress.compare_exchange_strong(expectedSendInProgress, false);
        return e->HResult;
    }

    return E_PENDING;
}
CATCH_RETURN()

HRESULT WebsockSendMessageGetResult(_In_ const XAsyncProviderData* data)
{
    if (data->context == nullptr ||
        data->bufferSize < sizeof(WebSocketCompletionResult))
    {
        return E_INVALIDARG;
    }

    auto sendMsgContext = shared_ptr_cache::fetch<SendMessageCallbackContext>(data->context);
    if (sendMsgContext == nullptr)
    {
        return E_HC_NOT_INITIALISED;
    }

    auto msg = sendMsgContext->nextMessage;
    auto websocketHandle = sendMsgContext->websocketTask->m_websocketHandle;

    if (websocketHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: GetResult ", TO_ULL(websocketHandle->websocket->id));

    WebSocketCompletionResult result = {};
    result.websocket = websocketHandle;
    result.errorCode = (FAILED(msg->m_storeAsyncResult)) ? E_FAIL : S_OK;
    result.platformErrorCode = msg->m_storeAsyncResult;
    CopyMemory(data->buffer, &result, sizeof(WebSocketCompletionResult));

    return S_OK;
}

void MessageWebSocketSendMessage(
    _In_ std::shared_ptr<winrt_websocket_impl> websocketTask
    )
{
    std::shared_ptr<websocket_outgoing_message> msg;

    {
        std::lock_guard<std::recursive_mutex> lock(websocketTask->m_outgoingMessageQueueLock);
        if (websocketTask->m_outgoingMessageQueue.size() > 0)
        {
            msg = websocketTask->m_outgoingMessageQueue.front();
            websocketTask->m_outgoingMessageQueue.pop();
        }
    }
    if (msg == nullptr)
    {
        bool expectedSendInProgress = true;
        websocketTask->m_outgoingMessageSendInProgress.compare_exchange_strong(expectedSendInProgress, false);
        return;
    }

    std::shared_ptr<SendMessageCallbackContext> callbackContext = http_allocate_shared<SendMessageCallbackContext>();
    callbackContext->nextMessage = msg;
    callbackContext->websocketTask = websocketTask;
    void* rawContext = shared_ptr_cache::store<SendMessageCallbackContext>(callbackContext);
    if (rawContext == nullptr)
    {
        return;
    }

    HRESULT hr = XAsyncBegin(msg->m_asyncBlock, rawContext, HCWebSocketSendMessageAsync, __FUNCTION__,
        [](_In_ XAsyncOp op, _In_ const XAsyncProviderData* data)
    {
        switch (op)
        {
            case XAsyncOp::DoWork: return WebsockSendMessageDoWork(data->async, data->context);
            case XAsyncOp::GetResult: return WebsockSendMessageGetResult(data);

            case XAsyncOp::Cleanup: 
            {
                shared_ptr_cache::remove(data->context);
                break;
            }
        }

        return S_OK;
    });

    if (hr == S_OK)
    {
        hr = XAsyncSchedule(msg->m_asyncBlock, 0);
    }

    if (FAILED(hr))
    {
        bool expectedSendInProgress = true;
        websocketTask->m_outgoingMessageSendInProgress.compare_exchange_strong(expectedSendInProgress, false);
    }
}

HRESULT CALLBACK WinRTWebSocketDisconnect(
    _In_ HCWebsocketHandle websocketHandle,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(context);

    if (websocketHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<winrt_websocket_impl> websocketTask = std::dynamic_pointer_cast<winrt_websocket_impl>(websocketHandle->websocket->impl);
    if (websocketTask == nullptr || websocketTask->m_messageWebSocket == nullptr)
    {
        return E_UNEXPECTED;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", TO_ULL(websocketHandle->websocket->id));
    try
    {
        websocketTask->m_messageWebSocket->Close(static_cast<unsigned short>(closeStatus), nullptr);
    }
    catch (Platform::Exception^ e)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: Close failed = 0x%0.8x", TO_ULL(websocketTask->m_websocketHandle->websocket->id), e->HResult);
        return e->HResult;
    }

    return S_OK;
}

