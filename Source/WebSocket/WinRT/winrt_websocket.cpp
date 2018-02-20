// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../hcwebsocket.h"

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
    HC_TASK_HANDLE m_taskHandle;
    HC_SUBSYSTEM_ID m_taskSubsystemId;
    uint64_t m_taskGroupId;
    void* m_completionRoutineContext;
    HCWebSocketCompletionRoutine m_completionRoutine;
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
    ReceiveContext() : m_websocket(nullptr)
    {
    }

    friend HC_RESULT WebsocketConnectExecute(
        _In_opt_ void* executionRoutineContext,
        _In_ HC_TASK_HANDLE taskHandle);

    void OnReceive(MessageWebSocket^ sender, MessageWebSocketMessageReceivedEventArgs^ args);
    void OnClosed(IWebSocket^ sender, WebSocketClosedEventArgs^ args);

private:
    HC_WEBSOCKET_HANDLE m_websocket;
};

class winrt_websocket_task : public xbox::httpclient::hc_task
{
public:
    winrt_websocket_task() : m_connectAsyncOpResult(S_OK)
    {
    }

    Windows::Networking::Sockets::MessageWebSocket^ m_messageWebSocket;
    Windows::Storage::Streams::DataWriter^ m_messageDataWriter;
    HRESULT m_connectAsyncOpResult;
    ReceiveContext^ m_context;

    IAsyncAction^ m_connectAsyncOp;
    AsyncStatus m_connectAsyncOpStatus;

    std::mutex m_outgoingMessageQueueLock;
    std::queue<std::shared_ptr<websocket_outgoing_message>> m_outgoingMessageQueue;
    HC_WEBSOCKET_HANDLE m_websocketHandle;
};

void MessageWebSocketSendMessage(
    _In_ std::shared_ptr<winrt_websocket_task> websocketTask
    );

void ReceiveContext::OnReceive(MessageWebSocket^ sender, MessageWebSocketMessageReceivedEventArgs^ args)
{
    try
    {
        DataReader^ reader = args->GetDataReader();
        const auto len = reader->UnconsumedBufferLength;
        if (len > 0)
        {
            std::string payload;
            payload.resize(len);
            reader->ReadBytes(Platform::ArrayReference<uint8_t>(reinterpret_cast<uint8 *>(&payload[0]), len));
            HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: receieved msg [%s]", m_websocket->id, payload.c_str());

            HC_WEBSOCKET_MESSAGE_FUNC messageFunc = nullptr;
            HCWebSocketGetFunctions(&messageFunc, nullptr);
            if (messageFunc != nullptr)
            {
                messageFunc(m_websocket, payload.c_str());
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
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: on closed event triggered", m_websocket->id);

    HC_WEBSOCKET_CLOSE_EVENT_FUNC closeFunc = nullptr;
    HCWebSocketGetFunctions(nullptr, &closeFunc);
    if (closeFunc != nullptr)
    {
        closeFunc(m_websocket, static_cast<HC_WEBSOCKET_CLOSE_STATUS>(args->Code));
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

HC_RESULT WebsocketConnectExecute(
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_HANDLE taskHandle
    )
try
{
    HC_WEBSOCKET_HANDLE websocket = static_cast<HC_WEBSOCKET_HANDLE>(executionRoutineContext);
    if (websocket == nullptr)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Websocket connect null call");
        return HC_E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Connect executing", websocket->id);

    std::shared_ptr<winrt_websocket_task> websocketTask = std::dynamic_pointer_cast<winrt_websocket_task>(websocket->task);
    websocketTask->m_messageWebSocket = ref new MessageWebSocket();

    uint32_t numHeaders = 0;
    HCWebSocketGetNumHeaders(websocket, &numHeaders);

    http_internal_string protocolHeader("Sec-WebSocket-Protocol");
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* headerName;
        const char* headerValue;
        HCWebSocketGetHeaderAtIndex(websocket, i, &headerName, &headerValue);

        // The MessageWebSocket API throws a COMException if you try to set the
        // 'Sec-WebSocket-Protocol' header here. It requires you to go through their API instead.
        if (headerName != nullptr && headerValue != nullptr && !str_icmp(headerName, protocolHeader.c_str()))
        {
            http_internal_wstring wHeaderName = utf16_from_utf8(headerName);
            http_internal_wstring wHeaderValue = utf16_from_utf8(headerValue);
            websocketTask->m_messageWebSocket->SetRequestHeader(
                Platform::StringReference(wHeaderName.c_str()),
                Platform::StringReference(wHeaderValue.c_str()));
            HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Header %d [%s: %s]", websocket->id, i, headerName, headerValue);
        }
    }

    auto protocols = parse_subprotocols(websocket->subProtocol);
    for (const auto& value : protocols)
    {
        websocketTask->m_messageWebSocket->Control->SupportedProtocols->Append(Platform::StringReference(value.c_str()));
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Protocol [%S]", websocket->id, value.c_str());
    }

    websocketTask->m_context = ref new ReceiveContext();
    websocketTask->m_context->m_websocket = websocket;

    http_internal_wstring aUrl = utf16_from_utf8(websocket->uri);
    const auto cxUri = ref new Windows::Foundation::Uri(Platform::StringReference(aUrl.c_str()));

    websocketTask->m_messageWebSocket->MessageReceived += ref new TypedEventHandler<MessageWebSocket^, MessageWebSocketMessageReceivedEventArgs^>(websocketTask->m_context, &ReceiveContext::OnReceive);
    websocketTask->m_messageWebSocket->Closed += ref new TypedEventHandler<IWebSocket^, WebSocketClosedEventArgs^>(websocketTask->m_context, &ReceiveContext::OnClosed);

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: connecting to %s", websocket->id, websocket->uri.c_str());
    websocketTask->m_connectAsyncOp = websocketTask->m_messageWebSocket->ConnectAsync(cxUri);

    websocketTask->m_connectAsyncOp->Completed = ref new AsyncActionCompletedHandler(
        [websocket, websocketTask, taskHandle](Windows::Foundation::IAsyncAction^ asyncOp, Windows::Foundation::AsyncStatus status)
    {
        UNREFERENCED_PARAMETER(status);
        try
        {
            websocketTask->m_messageDataWriter = ref new DataWriter(websocketTask->m_messageWebSocket->OutputStream);
            websocketTask->m_connectAsyncOpResult = S_OK;
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
            HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: connect failed 0x%0.8x", websocket->id, websocketTask->m_connectAsyncOpResult);
        }
        else
        {
            HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu] connect complete", websocket->id);
        }
        HCTaskSetCompleted(taskHandle);
    });

    return HC_OK;
}
CATCH_RETURN()

HC_RESULT WebsockConnectWriteResults(
    _In_opt_ void* writeResultsRoutineContext,
    _In_ HC_TASK_HANDLE taskHandleId,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext
    )
try
{
    UNREFERENCED_PARAMETER(taskHandleId);
    HC_WEBSOCKET_HANDLE websocket = static_cast<HC_WEBSOCKET_HANDLE>(writeResultsRoutineContext);
    if (websocket != nullptr)
    {
        HC_TRACE_INFORMATION(WEBSOCKET, "WebsockConnectWriteResults [ID %llu]", websocket->id);

        HCWebSocketCompletionRoutine completeFn = static_cast<HCWebSocketCompletionRoutine>(completionRoutine);
        if (completeFn != nullptr)
        {
            std::shared_ptr<winrt_websocket_task> websocketTask = std::dynamic_pointer_cast<winrt_websocket_task>(websocket->task);

            HC_RESULT hr = (FAILED(websocketTask->m_connectAsyncOpResult)) ? HC_E_FAIL : HC_OK;
            completeFn(completionRoutineContext, websocket, hr, websocketTask->m_connectAsyncOpResult);
        }
    }

    return HC_OK;
}
CATCH_RETURN()


HC_RESULT Internal_HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCWebSocketCompletionRoutine completionRoutine
    )
{
    std::shared_ptr<winrt_websocket_task> websocketTask = std::make_shared<winrt_websocket_task>();
    websocketTask->m_websocketHandle = websocket;
    websocket->uri = uri;
    websocket->subProtocol = subProtocol;
    websocket->task = std::dynamic_pointer_cast<xbox::httpclient::hc_task>(websocketTask);
    
    return HCTaskCreate(
        taskSubsystemId,
        taskGroupId,
        WebsocketConnectExecute, static_cast<void*>(websocket),
        WebsockConnectWriteResults, static_cast<void*>(websocket),
        completionRoutine, completionRoutineContext,
        nullptr);
}

HC_RESULT Internal_HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message,
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ void* completionRoutineContext,
    _In_opt_ HCWebSocketCompletionRoutine completionRoutine
    )
{
    if (message == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(false);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;
    std::shared_ptr<winrt_websocket_task> websocketTask = std::dynamic_pointer_cast<winrt_websocket_task>(websocket->task);

    std::shared_ptr<websocket_outgoing_message> msg = std::make_shared<websocket_outgoing_message>();
    msg->m_message = message;
    msg->m_taskSubsystemId = taskSubsystemId;
    msg->m_taskGroupId = taskGroupId;
    msg->m_completionRoutineContext = completionRoutineContext;
    msg->m_completionRoutine = completionRoutine;
    msg->m_id = ++httpSingleton->m_lastId;

    if (msg->m_message.length() == 0)
    {
        return HC_E_INVALIDARG;
    }

    bool sendInProgress = false;
    {
        std::lock_guard<std::mutex> lock(websocketTask->m_outgoingMessageQueueLock);
        if (websocketTask->m_outgoingMessageQueue.size() > 0)
        {
            sendInProgress = true;
        }
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: send msg queue size: %d", websocketTask->m_websocketHandle->id, websocketTask->m_outgoingMessageQueue.size());

        websocketTask->m_outgoingMessageQueue.push(msg);
    }

    // No sends in progress, so start sending the message
    if (!sendInProgress)
    {
        MessageWebSocketSendMessage(websocketTask);
    }
    
    return HC_OK;
}

struct SendMessageCallbackContent
{
    std::shared_ptr<websocket_outgoing_message> nextMessage;
    std::shared_ptr<winrt_websocket_task> websocketTask;
};

HC_RESULT WebsockSendMessageExecute(
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_HANDLE taskHandle
)
try
{
    auto sendMsgContext = shared_ptr_cache::fetch<SendMessageCallbackContent>(executionRoutineContext, false);
    if (sendMsgContext == nullptr)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Websocket: Send message execute null");
        return HC_E_INVALIDARG;
    }

    auto websocketTask = sendMsgContext->websocketTask;
    auto websocket = websocketTask->m_websocketHandle;
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Send message executing", websocket->id);

    auto msg = sendMsgContext->nextMessage;
    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Message [ID %llu] [%s]", websocket->id, msg->m_id, msg->m_message.c_str());

    websocketTask->m_messageWebSocket->Control->MessageType = SocketMessageType::Utf8;
    unsigned char* uchar = reinterpret_cast<unsigned char*>(const_cast<char*>(msg->m_message.c_str()));
    websocketTask->m_messageDataWriter->WriteBytes(Platform::ArrayReference<unsigned char>(uchar, static_cast<unsigned int>(msg->m_message.length())));

    msg->m_storeAsyncOp = websocketTask->m_messageDataWriter->StoreAsync();

    msg->m_storeAsyncOp->Completed = ref new AsyncOperationCompletedHandler<unsigned int>(
        [websocketTask, msg, taskHandle](Windows::Foundation::IAsyncOperation<unsigned int>^ asyncOp, Windows::Foundation::AsyncStatus status)
    {
        try
        {
            msg->m_storeAsyncOpStatus = status;
            unsigned int result = asyncOp->GetResults();
            HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: Message [ID %llu] send complete = %d", websocketTask->m_websocketHandle->id, msg->m_id, result);
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
            HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: Message [ID %llu] send failed = 0x%0.8x", websocketTask->m_websocketHandle->id, msg->m_id, msg->m_storeAsyncResult);
        }
        HCTaskSetCompleted(taskHandle);
        MessageWebSocketSendMessage(websocketTask);
    });

    return HC_OK;
}
CATCH_RETURN()


HC_RESULT WebsockSendMessageWriteResults(
    _In_opt_ void* writeResultsRoutineContext,
    _In_ HC_TASK_HANDLE taskHandleId,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext
    )
try
{
    UNREFERENCED_PARAMETER(taskHandleId);
    auto sendMsgContext = shared_ptr_cache::fetch<SendMessageCallbackContent>(writeResultsRoutineContext);
    if (sendMsgContext == nullptr)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Websocket write result null call");
        return HC_E_INVALIDARG;
    }

    auto msg = sendMsgContext->nextMessage;
    auto call = sendMsgContext->websocketTask->m_websocketHandle;

    if (call != nullptr)
    {
        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: calling send msg callback", call->id);

        HCWebSocketCompletionRoutine completeFn = static_cast<HCWebSocketCompletionRoutine>(completionRoutine);
        if (completeFn != nullptr)
        {
            HC_RESULT hr = (FAILED(msg->m_storeAsyncResult)) ? HC_E_FAIL : HC_OK;
            completeFn(completionRoutineContext, call, hr, msg->m_storeAsyncResult);
        }
    }
    return HC_OK;
}
CATCH_RETURN()


void MessageWebSocketSendMessage(
    _In_ std::shared_ptr<winrt_websocket_task> websocketTask
    )
{
    std::shared_ptr<websocket_outgoing_message> msg;

    {
        std::lock_guard<std::mutex> lock(websocketTask->m_outgoingMessageQueueLock);
        if (websocketTask->m_outgoingMessageQueue.size() > 0)
        {
            msg = websocketTask->m_outgoingMessageQueue.front();
            websocketTask->m_outgoingMessageQueue.pop();
        }
    }
    if (msg == nullptr)
    {
        return;
    }

    std::shared_ptr<SendMessageCallbackContent> callbackContext = std::make_shared<SendMessageCallbackContent>();
    callbackContext->nextMessage = msg;
    callbackContext->websocketTask = websocketTask;
    void* rawMsg = shared_ptr_cache::store<SendMessageCallbackContent>(callbackContext);

    HCTaskCreate(
        msg->m_taskSubsystemId,
        msg->m_taskGroupId,
        WebsockSendMessageExecute, static_cast<void*>(rawMsg),
        WebsockSendMessageWriteResults, static_cast<void*>(rawMsg),
        msg->m_completionRoutine, msg->m_completionRoutineContext,
        nullptr
        );
}


HC_RESULT Internal_HCWebSocketDisconnect(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CLOSE_STATUS closeStatus
    )
{
    if (websocket == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    std::shared_ptr<winrt_websocket_task> websocketTask = std::dynamic_pointer_cast<winrt_websocket_task>(websocket->task);
    if (websocketTask == nullptr || websocketTask->m_messageWebSocket == nullptr)
    {
        return HC_E_NOTINITIALISED;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", websocket->id);
    websocketTask->m_messageWebSocket->Close(static_cast<unsigned short>(closeStatus), nullptr);
    return HC_OK;
}

