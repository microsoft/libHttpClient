// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#if !HC_NOWEBSOCKETS

#include "hcwebsocket.h"

using namespace xbox::httpclient;

#define WEBSOCKET_RECVBUFFER_MAXSIZE_DEFAULT (1024 * 20)

HC_WEBSOCKET::HC_WEBSOCKET(
    _In_ uint64_t _id,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* functionContext
) :
    id{ _id },
    m_clientMessageFunc{ messageFunc },
    m_clientBinaryMessageFunc{ binaryMessageFunc },
    m_clientCloseEventFunc{ closeFunc },
    m_clientContext{ functionContext },
    m_maxReceiveBufferSize{ WEBSOCKET_RECVBUFFER_MAXSIZE_DEFAULT }
{
}

HC_WEBSOCKET::~HC_WEBSOCKET()
{
#if !HC_NOWEBSOCKETS
    HC_TRACE_VERBOSE(WEBSOCKET, "HCWebsocketHandle dtor");
#endif
}

HRESULT HC_WEBSOCKET::Connect(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    auto httpSingleton = get_http_singleton();
    if (!httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    if (m_state != State::Initial)
    {
        return E_UNEXPECTED;
    }

    m_uri = uri;
    m_subProtocol = subProtocol;

    WebSocketPerformInfo const& info = httpSingleton->m_websocketPerform;

    auto connectFunc = info.connect;
    if (connectFunc != nullptr)
    {
        try
        {
            // Trap the result of the connect before returning to client. Otherwise we have no way of knowing
            // if we are in a connected state. This also helps us handle proper disconnection if we were connecting,
            // when the client closed their handle.

            ZeroMemory(&m_connectAsyncBlock, sizeof(XAsyncBlock));
            m_connectAsyncBlock.queue = asyncBlock->queue;
            m_connectAsyncBlock.context = this;
            m_connectAsyncBlock.callback = [](XAsyncBlock* async)
            {
                auto thisPtr{ static_cast<HC_WEBSOCKET*>(async->context) };
                HRESULT hr = HCGetWebSocketConnectResult(async, &thisPtr->m_connectResult);

                // We auto disconnect if the client has already closed all handles.
                bool doDisconnect{ false };

                // We release the provider's reference if we don't expect any more callbacks from the WebSocket provider.
                bool doDecRef { false };
                {
                    std::lock_guard<std::recursive_mutex> lock{ thisPtr->m_mutex };

                    if (thisPtr->m_state != State::Connecting)
                    {
                        hr = E_UNEXPECTED;
                    }

                    if (SUCCEEDED(hr) && SUCCEEDED(thisPtr->m_connectResult.errorCode))
                    {
                        if (thisPtr->m_clientRefCount > 0)
                        {
                            thisPtr->m_state = State::Connected;
                        }
                        else
                        {
                            doDisconnect = true;
                        }
                    }
                    else
                    {
                        HC_TRACE_INFORMATION(WEBSOCKET, "Websocket connection attempt failed");

                        // Release providers ref if connect fails. We do not expect a close event in this case.
                        thisPtr->m_state = State::Disconnected;
                        doDecRef = true;
                    }
                }

                if (doDisconnect)
                {
                    thisPtr->Disconnect();
                }

                if (doDecRef)
                {
                    thisPtr->DecRef();
                }

                XAsyncComplete(thisPtr->m_clientConnectAsyncBlock, hr, sizeof(WebSocketCompletionResult));
            };

            m_clientConnectAsyncBlock = asyncBlock;
            XAsyncBegin(m_clientConnectAsyncBlock, this, (void*)HCWebSocketConnectAsync, __FUNCTION__,
                [](XAsyncOp op, const XAsyncProviderData* data)
                {
                    auto thisPtr{ static_cast<HC_WEBSOCKET*>(data->context) };

                    switch (op)
                    {
                    case XAsyncOp::DoWork: return E_PENDING;
                    case XAsyncOp::GetResult:
                    {
                        auto clientResult{ reinterpret_cast<WebSocketCompletionResult*>(data->buffer) };
                        *clientResult = thisPtr->m_connectResult;
                    }
                    default: return S_OK;
                    }
                }
            );

            HRESULT hr = connectFunc(uri, subProtocol, this, &m_connectAsyncBlock, info.context, httpSingleton->m_performEnv.get());

            if (SUCCEEDED(hr))
            {
                {
                    std::lock_guard<std::recursive_mutex> lock{ m_mutex };
                    m_state = State::Connecting;
                }
                // Add a ref for the provider. This guarantees the HC_WEBSOCKET is alive until disconnect.
                AddRef();
            }
            return hr;
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketConnect [ID %llu]: failed", TO_ULL(id));
            return E_FAIL;
        }
    }
    else
    {
        HC_TRACE_ERROR(WEBSOCKET, "HC_WEBSOCKET::Connect [ID %llu]: Websocket connect implementation not found!", TO_ULL(id));
        return E_UNEXPECTED;
    }
}

void notify_websocket_routed_handlers(
    std::shared_ptr<http_singleton> httpSingleton,
    _In_ HCWebsocketHandle websocket,
    _In_ bool receiving,
    _In_opt_z_ const char* message,
    _In_opt_ const uint8_t* payloadBytes,
    _In_ size_t payloadSize
    )
{
    if (httpSingleton == nullptr)
    {
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
    for (const auto& pair : httpSingleton->m_webSocketRoutedHandlers)
    {
        pair.second.first(
            websocket,
            receiving,
            message,
            payloadBytes,
            payloadSize,
            pair.second.second);
    }
}

HRESULT HC_WEBSOCKET::Send(
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    if (m_state != State::Connected)
    {
        return E_UNEXPECTED;
    }

    WebSocketPerformInfo const& info = httpSingleton->m_websocketPerform;

    auto sendFunc = info.sendText;
    if (sendFunc != nullptr)
    {
        try
        {
            notify_websocket_routed_handlers(httpSingleton, this, false, message, nullptr, 0);
            return sendFunc(this, message, asyncBlock, info.context);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketSendMessage [ID %llu]: failed", TO_ULL(id));
            return E_FAIL;
        }
    }
    else
    {
        HC_TRACE_ERROR(WEBSOCKET, "HC_WEBSOCKET::Send [ID %llu]: Websocket send implementation not found!", TO_ULL(id));
        return E_UNEXPECTED;
    }
    return S_OK;
}

HRESULT HC_WEBSOCKET::SendBinary(
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    if (m_state != State::Connected)
    {
        return E_UNEXPECTED;
    }

    WebSocketPerformInfo const& info = httpSingleton->m_websocketPerform;

    auto sendFunc = info.sendBinary;
    if (sendFunc != nullptr)
    {
        try
        {
            notify_websocket_routed_handlers(httpSingleton, this, false, nullptr, payloadBytes, payloadSize);
            return sendFunc(this, payloadBytes, payloadSize, asyncBlock, info.context);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketSendBinaryMessageAsync [ID %llu]: failed", TO_ULL(id));
            return E_FAIL;
        }
    }
    else
    {
        HC_TRACE_ERROR(WEBSOCKET, "HC_WEBSOCKET::Send [ID %llu]: Websocket send implementation not found!", TO_ULL(id));
        return E_UNEXPECTED;
    }
}

HRESULT HC_WEBSOCKET::Disconnect()
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    if (m_state != State::Connected)
    {
        return E_UNEXPECTED;
    }

    WebSocketPerformInfo const& info = httpSingleton->m_websocketPerform;

    auto disconnectFunc = info.disconnect;
    if (disconnectFunc != nullptr)
    {
        try
        {
            HRESULT hr = disconnectFunc(this, HCWebSocketCloseStatus::Normal, info.context);
            if (SUCCEEDED(hr))
            {
                std::lock_guard<std::recursive_mutex> lock{ m_mutex };
                m_state = State::Disconnecting;
            }
            return hr;
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "HCWebSocketClose [ID %llu]: failed", TO_ULL(id));
            return E_FAIL;
        }
    }
    else
    {
        HC_TRACE_ERROR(WEBSOCKET, "HC_WEBSOCKET::Disconnect [ID %llu]: Websocket disconnect implementation not found!", TO_ULL(id));
        return E_UNEXPECTED;
    }
}

const HttpHeaders& HC_WEBSOCKET::Headers() const noexcept
{
    return m_connectHeaders;
}

const http_internal_string& HC_WEBSOCKET::ProxyUri() const noexcept
{
    return m_proxyUri;
}

const bool HC_WEBSOCKET::ProxyDecryptsHttps() const noexcept
{
    return m_allowProxyToDecryptHttps;
}

const http_internal_string& HC_WEBSOCKET::Uri() const noexcept
{
    return m_uri;
}

const http_internal_string& HC_WEBSOCKET::SubProtocol() const noexcept
{
    return m_subProtocol;
}

size_t HC_WEBSOCKET::MaxReceiveBufferSize() const noexcept
{
    return m_maxReceiveBufferSize;
}

HRESULT HC_WEBSOCKET::SetBinaryMessageFragmentFunc(HCWebSocketBinaryMessageFragmentFunction func) noexcept
{
    std::lock_guard<std::recursive_mutex> lock{ m_mutex };
    m_clientBinaryMessageFragmentFunc = func;
    return S_OK;
}

HRESULT HC_WEBSOCKET::SetProxyUri(
    http_internal_string&& proxyUri
) noexcept
{
    if (m_state != State::Initial)
    {
        return E_HC_CONNECT_ALREADY_CALLED;
    }
    m_proxyUri = std::move(proxyUri);
    m_allowProxyToDecryptHttps = false;
    return S_OK;
}

HRESULT HC_WEBSOCKET::SetProxyDecryptsHttps(
    bool allowProxyToDecryptHttps
    ) noexcept
{
    if (m_proxyUri.empty())
    {
        return E_UNEXPECTED;
    }
    m_allowProxyToDecryptHttps = allowProxyToDecryptHttps;
    return S_OK;
}

HRESULT HC_WEBSOCKET::SetHeader(
    http_internal_string&& headerName,
    http_internal_string&& headerValue
) noexcept
{
    if (m_state != State::Initial)
    {
        return E_HC_CONNECT_ALREADY_CALLED;
    }
    m_connectHeaders[headerName] = headerValue;
    return S_OK;
}

HRESULT HC_WEBSOCKET::SetMaxReceiveBufferSize(size_t maxReceiveBufferSizeBytes) noexcept
{
    m_maxReceiveBufferSize = maxReceiveBufferSizeBytes;
    return S_OK;
}

void HC_WEBSOCKET::AddClientRef()
{
    {
        std::lock_guard<std::recursive_mutex> lock{ m_mutex };
        ++m_clientRefCount;
    }
    AddRef();
}

void HC_WEBSOCKET::DecClientRef()
{
    bool doDisconnect{ false };
    {
        std::lock_guard<std::recursive_mutex> lock{ m_mutex };
        if (--m_clientRefCount == 0)
        {
            if (m_state == State::Connected)
            {
                HC_TRACE_WARNING(WEBSOCKET, "No client reference remain for HC_WEBSOCKET but it is either connected/connecting. Disconnecting now.");
                doDisconnect = true;
            }
        }
    }

    if (doDisconnect)
    {
        HRESULT hr = Disconnect();
        if (FAILED(hr))
        {
            HC_TRACE_WARNING(WEBSOCKET, "Disconnect failed with hresult hr=%u", hr);
        }
    }

    DecRef();
}

void HC_WEBSOCKET::AddRef()
{
    if (m_totalRefCount++ == 0)
    {
        m_extraRefHolder = shared_from_this();
    }
}

void HC_WEBSOCKET::DecRef()
{
    if (--m_totalRefCount == 0)
    {
        m_extraRefHolder.reset();
    }
}

void HC_WEBSOCKET::MessageFunc(
    HC_WEBSOCKET* websocket,
    const char* message,
    void* context
)
{
    UNREFERENCED_PARAMETER(context);
    std::unique_lock<std::recursive_mutex> lock{ websocket->m_mutex };
    if (websocket->m_clientRefCount > 0)
    {
        try
        {
            if (websocket->m_clientMessageFunc)
            {
                auto httpSingleton = get_http_singleton();
                notify_websocket_routed_handlers(httpSingleton, websocket, true, message, nullptr, 0);
                lock.unlock();
                websocket->m_clientMessageFunc(websocket, message, websocket->m_clientContext);
            }
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketMessageFunction");
        }
    }
}

void HC_WEBSOCKET::BinaryMessageFunc(
    HC_WEBSOCKET* websocket,
    const uint8_t* bytes,
    uint32_t payloadSize,
    void* context
)
{
    UNREFERENCED_PARAMETER(context);
    std::unique_lock<std::recursive_mutex> lock{ websocket->m_mutex };
    if (websocket->m_clientRefCount > 0)
    {
        try
        {
            if (websocket->m_clientBinaryMessageFunc)
            {
                auto httpSingleton = get_http_singleton();
                notify_websocket_routed_handlers(httpSingleton, websocket, true, nullptr, bytes, payloadSize);
                lock.unlock();
                websocket->m_clientBinaryMessageFunc(websocket, bytes, payloadSize, websocket->m_clientContext);
            }
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketBinaryMessageFunction");
        }
    }
}

void HC_WEBSOCKET::BinaryMessageFragmentFunc(
    HC_WEBSOCKET* websocket,
    const uint8_t* bytes,
    uint32_t payloadSize,
    bool isLastFragment,
    void* /*context*/
)
{
    std::unique_lock<std::recursive_mutex> lock{ websocket->m_mutex };
    if (websocket->m_clientRefCount > 0)
    {
        try
        {
            auto httpSingleton = get_http_singleton();
            notify_websocket_routed_handlers(httpSingleton, websocket, true, nullptr, bytes, payloadSize);
            lock.unlock();

            if (websocket->m_clientBinaryMessageFragmentFunc)
            {
                websocket->m_clientBinaryMessageFragmentFunc(websocket, bytes, payloadSize, isLastFragment, websocket->m_clientContext);
            }
            else if (websocket->m_clientBinaryMessageFunc)
            {
                HC_TRACE_INFORMATION(WEBSOCKET, "Received binary message fragment but no client handler has been set. Invoking HCWebSocketBinaryMessageFunction instead.");
                websocket->m_clientBinaryMessageFunc(websocket, bytes, payloadSize, websocket->m_clientContext);
            }
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketBinaryMessageFragmentFunction");
        }
    }
}

void HC_WEBSOCKET::CloseFunc(
    HC_WEBSOCKET* websocket,
    HCWebSocketCloseStatus status,
    void* context
)
{
    UNREFERENCED_PARAMETER(context);

    // We release the provider's reference if we handle the close.
    bool shouldDecRef { false };
    {
        std::unique_lock<std::recursive_mutex> lock{ websocket->m_mutex };

        auto state = websocket->m_state;
        websocket->m_state = State::Disconnected;

        switch (state)
        {
        case State::Initial:
            // provider error: should not be calling CloseFunc when websocket connection hasn't been created.
            break;
        case State::Connecting:
            // provider error: providers should complete connect async context rather than calling closeFunc if a connect attempt fails.
            break;
        case State::Disconnected:
            // provider error: providers should call closeFunc exactly once per successful attempt.
            break;
        case State::Connected:
        case State::Disconnecting:
            shouldDecRef = true;
            if (websocket->m_clientRefCount > 0)
            {
                try
                {
                    if (websocket->m_clientCloseEventFunc)
                    {
                        lock.unlock();
                        websocket->m_clientCloseEventFunc(websocket, status, websocket->m_clientContext);
                    }
                }
                catch (...)
                {
                    HC_TRACE_WARNING(WEBSOCKET, "Caught exception in client HCWebSocketCloseEventFunction");
                }
            }
            break;
        }
    }

    if (shouldDecRef)
    {
        // Release the providers ref
        websocket->DecRef();
    }
}

STDAPI
HCWebSocketCreate(
    _Out_ HCWebsocketHandle* websocket,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* functionContext
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    std::shared_ptr<HC_WEBSOCKET> socket = http_allocate_shared<HC_WEBSOCKET>(
        ++httpSingleton->m_lastId,
        messageFunc,
        binaryMessageFunc,
        closeFunc,
        functionContext
    );

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCreate [ID %llu]", TO_ULL(socket->id));

    socket->AddClientRef();
    *websocket = socket.get();
    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketSetBinaryMessageFragmentEventFunction(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketBinaryMessageFragmentFunction binaryMessageFragmentFunc
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !websocket);
    return websocket->SetBinaryMessageFragmentFunc(binaryMessageFragmentFunc);
}
CATCH_RETURN()

STDAPI
HCWebSocketSetProxyUri(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* proxyUri
    ) noexcept
try
{
    if (websocket == nullptr || proxyUri == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->SetProxyUri(proxyUri);
}
CATCH_RETURN()

STDAPI
HCWebSocketSetProxyDecryptsHttps(
    _In_ HCWebsocketHandle websocket,
    _In_z_ bool allowProxyToDecryptHttps
) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->SetProxyDecryptsHttps(allowProxyToDecryptHttps);
}
CATCH_RETURN()

STDAPI
HCWebSocketSetHeader(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue
    ) noexcept
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->SetHeader(headerName, headerValue);
}
CATCH_RETURN()

STDAPI
HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept
try
{
    if (uri == nullptr || websocket == nullptr || subProtocol == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->Connect(uri, subProtocol, asyncBlock);
}
CATCH_RETURN()

STDAPI
HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept
try
{
    if (message == nullptr || websocket == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->Send(message, asyncBlock);
}
CATCH_RETURN()

STDAPI
HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept
try
{
    if (payloadBytes == nullptr || payloadSize == 0 || websocket == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->SendBinary(payloadBytes, payloadSize, asyncBlock);
}
CATCH_RETURN()

STDAPI
HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }
    return websocket->Disconnect();
}
CATCH_RETURN()

STDAPI HCWebSocketSetMaxReceiveBufferSize(
    _In_ HCWebsocketHandle websocket,
    _In_ size_t bufferSizeInBytes
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !websocket);
    return websocket->SetMaxReceiveBufferSize(bufferSizeInBytes);
}
CATCH_RETURN()

STDAPI_(HCWebsocketHandle) HCWebSocketDuplicateHandle(
    _In_ HCWebsocketHandle websocket
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketDuplicateHandle [ID %llu]", TO_ULL(websocket->id));
    websocket->AddClientRef();

    return websocket;
}
CATCH_RETURN_WITH(nullptr)

STDAPI
HCWebSocketCloseHandle(
    _In_ HCWebsocketHandle websocket
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "HCWebSocketCloseHandle [ID %llu]", TO_ULL(websocket->id));
    websocket->DecClientRef();

    return S_OK;
}
CATCH_RETURN()

STDAPI
HCSetWebSocketFunctions(
    _In_ HCWebSocketConnectFunction websocketConnectFunc,
    _In_ HCWebSocketSendMessageFunction websocketSendMessageFunc,
    _In_ HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc,
    _In_ HCWebSocketDisconnectFunction websocketDisconnectFunc,
    _In_opt_ void* context
    ) noexcept
try
{
    if (websocketConnectFunc == nullptr ||
        websocketSendMessageFunc == nullptr ||
        websocketSendBinaryMessageFunc == nullptr ||
        websocketDisconnectFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (httpSingleton)
    {
        return E_HC_ALREADY_INITIALISED;
    }

    auto& info = GetUserWebSocketPerformHandlers();

    info.connect = websocketConnectFunc;
    info.sendText = websocketSendMessageFunc;
    info.sendBinary = websocketSendBinaryMessageFunc;
    info.disconnect = websocketDisconnectFunc;
    info.context = context;
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCGetWebSocketFunctions(
    _Out_ HCWebSocketConnectFunction* websocketConnectFunc,
    _Out_ HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    _Out_ HCWebSocketSendBinaryMessageFunction* websocketSendBinaryMessageFunc,
    _Out_ HCWebSocketDisconnectFunction* websocketDisconnectFunc,
    _Out_ void** context
) noexcept
try
{
    if (websocketConnectFunc == nullptr ||
        websocketSendMessageFunc == nullptr ||
        websocketSendBinaryMessageFunc == nullptr ||
        websocketDisconnectFunc == nullptr ||
        context == nullptr)
    {
        return E_INVALIDARG;
    }

    auto const& info = GetUserWebSocketPerformHandlers();

    *websocketConnectFunc = info.connect;
    *websocketSendMessageFunc = info.sendText;
    *websocketSendBinaryMessageFunc = info.sendBinary;
    *websocketDisconnectFunc = info.disconnect;
    *context = info.context;
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketGetProxyUri(
    _In_ HCWebsocketHandle websocket,
    _Out_ const char** proxyUri
) noexcept
try
{
    if (websocket == nullptr || proxyUri == nullptr)
    {
        return E_INVALIDARG;
    }

    *proxyUri = websocket->ProxyUri().data();
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketGetHeader(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
) noexcept
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    auto& headers{ websocket->Headers() };
    auto it = headers.find(headerName);
    if (it != headers.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketGetNumHeaders(
    _In_ HCWebsocketHandle websocket,
    _Out_ uint32_t* numHeaders
    ) noexcept
try
{
    if (websocket == nullptr || numHeaders == nullptr)
    {
        return E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(websocket->Headers().size());
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketGetHeaderAtIndex(
    _In_ HCWebsocketHandle websocket,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
    ) noexcept
try
{
    if (websocket == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    uint32_t index = 0;
    auto& headers{ websocket->Headers() };
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return S_OK;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCWebSocketGetEventFunctions(
    _In_ HCWebsocketHandle websocket,
    _Out_opt_ HCWebSocketMessageFunction* messageFunc,
    _Out_opt_ HCWebSocketBinaryMessageFunction* binaryMessageFunc,
    _Out_opt_ HCWebSocketCloseEventFunction* closeFunc,
    _Out_ void** context
    ) noexcept
try
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    if (messageFunc != nullptr)
    {
        *messageFunc = HC_WEBSOCKET::MessageFunc;
    }

    if (binaryMessageFunc != nullptr)
    {
        *binaryMessageFunc = HC_WEBSOCKET::BinaryMessageFunc;
    }

    if (closeFunc != nullptr)
    {
        *closeFunc = HC_WEBSOCKET::CloseFunc;
    }

    if (context != nullptr)
    {
        *context = nullptr;
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI HCWebSocketGetBinaryMessageFragmentEventFunction(
    _In_ HCWebsocketHandle websocket,
    _Out_ HCWebSocketBinaryMessageFragmentFunction* binaryMessageFragmentFunc,
    _Out_ void** context
) noexcept
try
{
    RETURN_HR_IF(E_INVALIDARG, !websocket || !binaryMessageFragmentFunc || !context);

    *binaryMessageFragmentFunc = HC_WEBSOCKET::BinaryMessageFragmentFunc;
    *context = nullptr;
    return S_OK;
}
CATCH_RETURN()


STDAPI
HCGetWebSocketConnectResult(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) noexcept
try
{
    return XAsyncGetResult(
        asyncBlock,
        reinterpret_cast<void*>(HCWebSocketConnectAsync),
        sizeof(WebSocketCompletionResult),
        result,
        nullptr
    );
}
CATCH_RETURN()

STDAPI
HCGetWebSocketSendMessageResult(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ WebSocketCompletionResult* result
    ) noexcept
try
{
    return XAsyncGetResult(
        asyncBlock,
        reinterpret_cast<void*>(HCWebSocketSendMessageAsync),
        sizeof(WebSocketCompletionResult),
        result,
        nullptr
    );
}
CATCH_RETURN()

#endif

