// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "hcwebsocket.h"
#include "websocket_options.h"

#ifndef HC_NOWEBSOCKETS

using namespace xbox::httpclient;

#define WEBSOCKET_RECVBUFFER_MAXSIZE_DEFAULT (1024 * 20)

HC_WEBSOCKET_OBSERVER::HC_WEBSOCKET_OBSERVER(std::shared_ptr<xbox::httpclient::WebSocket> websocket) : websocket{ std::move(websocket) }
{
}

HC_WEBSOCKET_OBSERVER::~HC_WEBSOCKET_OBSERVER()
{
    HC_TRACE_INFORMATION(WEBSOCKET, __FUNCTION__);

    websocket->UnregisterEventCallbacks(m_handlerToken);
}

int HC_WEBSOCKET_OBSERVER::AddRef() noexcept
{
    return ++m_refCount;
}

int HC_WEBSOCKET_OBSERVER::Release() noexcept
{
    int count = --m_refCount;
    if (count == 0)
    {
        this->~HC_WEBSOCKET_OBSERVER();
        http_stl_allocator<HC_WEBSOCKET_OBSERVER> alloc;
        std::allocator_traits<http_stl_allocator<HC_WEBSOCKET_OBSERVER>>::deallocate(alloc, this, 1);
    }
    else if (count < 0)
    {
        assert(false);
    }
    return count;
}

xbox::httpclient::ObserverPtr HC_WEBSOCKET_OBSERVER::Initialize(
    _In_ std::shared_ptr<xbox::httpclient::WebSocket> websocket,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* callbackContext
)
{
    http_stl_allocator<HC_WEBSOCKET_OBSERVER> a{};
    xbox::httpclient::ObserverPtr observer{ new (a.allocate(1)) HC_WEBSOCKET_OBSERVER{ std::move(websocket) } };

    observer->m_messageFunc = messageFunc;
    observer->m_binaryMessageFunc = binaryMessageFunc;
    observer->m_binaryFragmentFunc = binaryFragmentFunc;
    observer->m_closeFunc = closeFunc;
    observer->m_callbackContext = callbackContext;
    observer->m_handlerToken = observer->websocket->RegisterEventCallbacks(MessageFunc, BinaryMessageFunc, BinaryMessageFragmentFunc, CloseFunc, observer.get());

    return observer;
}

HRESULT HC_WEBSOCKET_OBSERVER::SetBinaryMessageFragmentEventFunction(HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc) noexcept
{
    RETURN_HR_IF(E_NOT_SUPPORTED, binaryFragmentFunc != nullptr && websocket->UsesDeterministicSemantics());
    m_binaryFragmentFunc = binaryFragmentFunc;
    return S_OK;
}

bool HC_WEBSOCKET_OBSERVER::HasBinaryMessageFragmentEventFunction() const noexcept
{
    return m_binaryFragmentFunc != nullptr;
}

void CALLBACK HC_WEBSOCKET_OBSERVER::MessageFunc(HCWebsocketHandle internalHandle, const char* message, void* context)
{
    HC_WEBSOCKET_OBSERVER* observer{ static_cast<HC_WEBSOCKET_OBSERVER*>(context) };

    assert(internalHandle->websocket->id == observer->websocket->id);
    UNREFERENCED_PARAMETER(internalHandle);

    if (observer->m_messageFunc)
    {
        observer->m_messageFunc(observer, message, observer->m_callbackContext);
    }
}

void CALLBACK HC_WEBSOCKET_OBSERVER::BinaryMessageFunc(HCWebsocketHandle internalHandle, const uint8_t* bytes, uint32_t payloadSize, void* context)
{
    HC_WEBSOCKET_OBSERVER* observer{ static_cast<HC_WEBSOCKET_OBSERVER*>(context) };

    assert(internalHandle->websocket->id == observer->websocket->id);
    UNREFERENCED_PARAMETER(internalHandle);

    if (observer->m_binaryMessageFunc)
    {
        observer->m_binaryMessageFunc(observer, bytes, payloadSize, observer->m_callbackContext);
    }
}

void CALLBACK HC_WEBSOCKET_OBSERVER::BinaryMessageFragmentFunc(HCWebsocketHandle internalHandle, const uint8_t* payloadBytes, uint32_t payloadSize, bool isLastFragment, void* context)
{
    HC_WEBSOCKET_OBSERVER* observer{ static_cast<HC_WEBSOCKET_OBSERVER*>(context) };

    assert(internalHandle->websocket->id == observer->websocket->id);
    UNREFERENCED_PARAMETER(internalHandle);

    if (observer->m_binaryFragmentFunc)
    {
        observer->m_binaryFragmentFunc(observer, payloadBytes, payloadSize, isLastFragment, observer->m_callbackContext);
    }
}

void CALLBACK HC_WEBSOCKET_OBSERVER::CloseFunc(HCWebsocketHandle internalHandle, HCWebSocketCloseStatus status, void* context)
{
    HC_WEBSOCKET_OBSERVER* observer{ static_cast<HC_WEBSOCKET_OBSERVER*>(context) };

    assert(internalHandle->websocket->id == observer->websocket->id);
    UNREFERENCED_PARAMETER(internalHandle);

    if (observer->m_closeFunc)
    {
        observer->m_closeFunc(observer, status, observer->m_callbackContext);
    }
}

namespace xbox
{
namespace httpclient
{

WebSocket::WebSocket(uint64_t _id, IWebSocketProvider& provider) :
    id{ _id },
    m_maxReceiveBufferSize{ WEBSOCKET_RECVBUFFER_MAXSIZE_DEFAULT },
    m_provider{ provider }
{
}

WebSocket::~WebSocket()
{
    HC_TRACE_INFORMATION(WEBSOCKET, __FUNCTION__);
}

uint32_t WebSocket::RegisterEventCallbacks(
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* callbackContext
)
{
    std::unique_lock<std::recursive_mutex> lock{ m_eventCallbacksMutex };
    m_eventCallbacks[m_nextToken] = EventCallbacks{ messageFunc, binaryMessageFunc, binaryFragmentFunc, closeFunc, callbackContext };
    return m_nextToken++;
}

void WebSocket::UnregisterEventCallbacks(uint32_t registrationToken)
{
    std::unique_lock<std::recursive_mutex> lock{ m_eventCallbacksMutex };
    m_eventCallbacks.erase(registrationToken);
}

// Context for ConnectAsync operation. Ensures lifetime until Connect operation completes
struct WebSocket::ConnectContext
{
    ConnectContext(std::shared_ptr<WebSocket> websocket, XAsyncBlock* async) :
        observer{ HC_WEBSOCKET_OBSERVER::Initialize(std::move(websocket)) },
        clientAsyncBlock{ async },
        internalAsyncBlock{ nullptr, this, WebSocket::ConnectComplete }
    {
    }

    ~ConnectContext()
    {
        if (internalAsyncBlock.queue)
        {
            XTaskQueueCloseHandle(internalAsyncBlock.queue);
        }
    }

    xbox::httpclient::ObserverPtr observer;
    XAsyncBlock* const clientAsyncBlock;
    XAsyncBlock internalAsyncBlock;
    WebSocketCompletionResult result{};
};

// Context for Provider event callbacks. Ensure's lifetime as long as the WebSocket is connected (until CloseFunc is called)
struct WebSocket::ProviderContext
{
    xbox::httpclient::ObserverPtr observer;
};

HRESULT WebSocket::ConnectAsync(
    _In_ http_internal_string&& uri,
    _In_ http_internal_string&& subProtocol,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    m_uri = std::move(uri);
    m_subProtocol = std::move(subProtocol);

    auto context = http_allocate_unique<ConnectContext>(shared_from_this(), asyncBlock);
    RETURN_IF_FAILED(XAsyncBegin(asyncBlock, context.get(), (void*)HCWebSocketConnectAsync, nullptr, ConnectAsyncProvider));
    context.release();
    return S_OK;
}

HRESULT CALLBACK WebSocket::ConnectAsyncProvider(XAsyncOp op, XAsyncProviderData const* data)
{
    ConnectContext* context{ static_cast<ConnectContext*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        assert(context->observer);
        auto& ws{ context->observer->websocket };
        std::unique_lock<DefaultUnnamedMutex> lock{ ws->m_stateMutex };

        RETURN_HR_IF(E_UNEXPECTED, ws->m_state != State::Initial);
        ws->ClearResponseHeadersLockHeld();

        // Run the internal connect completion on the caller's Work port so it is not stranded when the
        // Completion port is dispatched asymmetrically. The client completion still fires on the
        // Completion port via the outer async block.
        XTaskQueuePortHandle workPort{ nullptr };
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->internalAsyncBlock.queue));

        ws->m_state = State::Connecting;
        lock.unlock();

        try
        {
            return ws->m_provider.ConnectAsync(ws->m_uri, ws->m_subProtocol, context->observer.get(), &context->internalAsyncBlock);
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketConnectFunction [ID %llu]", TO_ULL(ws->id));
            return E_FAIL;
        }
    }
    case XAsyncOp::GetResult:
    {
        WebSocketCompletionResult* result{ reinterpret_cast<WebSocketCompletionResult*>(data->buffer) };
        *result = context->result;
        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        HC_UNIQUE_PTR<ConnectContext> reclaim{ context };
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK WebSocket::ConnectComplete(XAsyncBlock* async)
{
    ConnectContext* context{ static_cast<ConnectContext*>(async->context) };
    auto& ws{ context->observer->websocket };

    // We can be put into the Disconnected state if a spontaneous error occurs between the connection process completing and this callback being invoked.
    // We need to be able to handle that scenario here.
    HRESULT hr = HCGetWebSocketConnectResult(&context->internalAsyncBlock, &context->result);

    std::unique_lock<DefaultUnnamedMutex> lock{ ws->m_stateMutex };
    const bool bIsDisconnected = (ws->m_state == State::Disconnected);
    if (bIsDisconnected && !FAILED(hr))
    {
        HC_TRACE_WARNING(WEBSOCKET, "WebSocket::ConnectComplete [%p] encountered a spontaneous disconnection.  This implies the connection process was successful, but we otherwise had to close the connection (handle=%p)", ws.get(), context->observer.get());
        hr = E_FAIL;
    }

    assert(ws->m_state == State::Connecting || bIsDisconnected);
    assert(context->observer.get() == context->result.websocket || FAILED(hr) || bIsDisconnected);
    if (SUCCEEDED(hr) && SUCCEEDED(context->result.errorCode))
    {
        // Connect was sucessful. Allocate ProviderContext to ensure WebSocket lifetime until it is reclaimed in WebSocket::CloseFunc
        ws->m_state = State::Connected;
        ws->m_providerContext = new (http_stl_allocator<ProviderContext>{}.allocate(1)) ProviderContext{
            std::move(context->observer)
        };
    }
    else
    {
        // Preserve cached response headers until handle close so failed upgrade diagnostics remain queryable.
        ws->m_state = State::Disconnected;
    }
    lock.unlock();

    XAsyncComplete(context->clientAsyncBlock, hr, sizeof(WebSocketCompletionResult));
}

HRESULT WebSocket::SendAsync(
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, m_state != State::Connected);
    RETURN_HR_IF(E_UNEXPECTED, !m_providerContext);

    try
    {
        NotifyWebSocketRoutedHandlers(m_providerContext->observer.get(), false, message, nullptr, 0);
        return m_provider.SendAsync(m_providerContext->observer.get(), message, asyncBlock);
    }
    catch (...)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketSendMessageFunction [ID %llu]", TO_ULL(id));
        return E_FAIL;
    }
}

HRESULT WebSocket::SendBinaryAsync(
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, m_state != State::Connected);
    RETURN_HR_IF(E_UNEXPECTED, !m_providerContext);

    try
    {
        NotifyWebSocketRoutedHandlers(m_providerContext->observer.get(), false, nullptr, payloadBytes, payloadSize);
        return m_provider.SendBinaryAsync(m_providerContext->observer.get(), payloadBytes, payloadSize, asyncBlock);
    }
    catch (...)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketSendBinaryMessageFunction [ID %llu]", TO_ULL(id));
        return E_FAIL;
    }
}

HRESULT WebSocket::Disconnect()
{
    return Disconnect(HCWebSocketCloseStatus::Normal);
}

HRESULT WebSocket::Disconnect(HCWebSocketCloseStatus closeStatus)
{
    RETURN_HR_IF(E_UNEXPECTED, !m_providerContext);

    std::unique_lock<DefaultUnnamedMutex> lock{ m_stateMutex };
    RETURN_HR_IF(S_OK, m_state == State::Disconnecting);
    RETURN_HR_IF(E_UNEXPECTED, m_state != State::Connected);
    
    m_state = State::Disconnecting;
    lock.unlock();

    try
    {
        return m_provider.Disconnect(m_providerContext->observer.get(), closeStatus);
    }
    catch (...)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketDisconnectFunction [ID %llu]", TO_ULL(id));
        return E_FAIL;
    }
}

const http_internal_string& WebSocket::Uri() const noexcept
{
    return m_uri;
}

const http_internal_string& WebSocket::SubProtocol() const noexcept
{
    return m_subProtocol;
}

const HttpHeaders& WebSocket::Headers() const noexcept
{
    return m_connectHeaders;
}

HRESULT WebSocket::GetResponseHeader(
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
) const noexcept
{
    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };

    auto it = m_connectResponseHeaders.find(headerName);
    if (it != m_connectResponseHeaders.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }

    return S_OK;
}

uint32_t WebSocket::GetNumResponseHeaders() const noexcept
{
    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };
    return static_cast<uint32_t>(m_connectResponseHeaders.size());
}

HRESULT WebSocket::GetResponseHeaderAtIndex(
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
) const noexcept
{
    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };

    uint32_t index = 0;
    for (auto it = m_connectResponseHeaders.cbegin(); it != m_connectResponseHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return S_OK;
        }

        ++index;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return E_INVALIDARG;
}

const http_internal_string& WebSocket::ProxyUri() const noexcept
{
    return m_proxyUri;
}

const bool WebSocket::ProxyDecryptsHttps() const noexcept
{
    return m_allowProxyToDecryptHttps;
}

HCWebSocketOptions WebSocket::Options() const noexcept
{
    return m_options;
}

size_t WebSocket::MaxReceiveBufferSize() const noexcept
{
    return m_maxReceiveBufferSize;
}

size_t WebSocket::DeterministicMaxReceiveBufferSize() const noexcept
{
    return m_maxReceiveBufferSizeExplicitlySet ?
        m_maxReceiveBufferSize :
        WEBSOCKET_RECVBUFFER_MAXSIZE_DETERMINISTIC_DEFAULT;
}

bool WebSocket::MaxReceiveBufferSizeExplicitlySet() const noexcept
{
    return m_maxReceiveBufferSizeExplicitlySet;
}

uint32_t WebSocket::PingInterval() const noexcept
{
    return m_pingInterval;
}

bool WebSocket::OptionsExplicitlySet() const noexcept
{
    return m_optionsExplicitlySet;
}

bool WebSocket::UsesLegacySemantics() const noexcept
{
    return !m_optionsExplicitlySet || RequestsLegacyWebSocketSemantics(m_options);
}

bool WebSocket::UsesDeterministicSemantics() const noexcept
{
    return m_optionsExplicitlySet && !RequestsLegacyWebSocketSemantics(m_options);
}

HRESULT WebSocket::SetHeader(
    http_internal_string&& headerName,
    http_internal_string&& headerValue
) noexcept
{
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_state != State::Initial);
    m_connectHeaders[headerName] = headerValue;
    return S_OK;
}

HRESULT WebSocket::SetProxyUri(
    http_internal_string&& proxyUri
) noexcept
{
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_state != State::Initial);
    m_proxyUri = std::move(proxyUri);
    m_allowProxyToDecryptHttps = false;
    return S_OK;
}

HRESULT WebSocket::SetProxyDecryptsHttps(
    bool allowProxyToDecryptHttps
) noexcept
{
    if (m_proxyUri.empty())
    {
        return E_UNEXPECTED;
    }
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_state != State::Initial);
    m_allowProxyToDecryptHttps = allowProxyToDecryptHttps;
    return S_OK;
}

HRESULT WebSocket::SetMaxReceiveBufferSize(size_t maxReceiveBufferSizeBytes) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, maxReceiveBufferSizeBytes == 0);
    RETURN_HR_IF(E_INVALIDARG, maxReceiveBufferSizeBytes > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()));

    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_state != State::Initial);

    m_maxReceiveBufferSize = maxReceiveBufferSizeBytes;
    m_maxReceiveBufferSizeExplicitlySet = true;
    return S_OK;
}

HRESULT WebSocket::SetPingInterval(uint32_t pingInterval) noexcept
{
    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_state != State::Initial);

    m_pingInterval = pingInterval;
    return S_OK;
}

HRESULT WebSocket::SetOptions(HCWebSocketOptions options) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, HasUnsupportedWebSocketOptions(options));
    RETURN_HR_IF(
        E_INVALIDARG,
        RequestsLegacyWebSocketSemantics(options) &&
            options != HCWebSocketOptions::LegacySemantics);
    RETURN_HR_IF(
        E_INVALIDARG,
        HasNoContextTakeoverWebSocketOptions(options) && !RequestsWebSocketCompression(options));

    // Check fragment handlers before acquiring m_stateMutex to avoid nesting
    // m_stateMutex -> m_eventCallbacksMutex (which HasBinaryMessageFragmentHandlers acquires).
    // Safe because SetOptions is only valid in State::Initial, before callbacks can fire.
    bool const hasBinaryFragmentHandlers = HasBinaryMessageFragmentHandlers();

    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_state != State::Initial);
    if (RequestsLegacyWebSocketSemantics(options))
    {
        m_options = options;
        m_optionsExplicitlySet = true;
        return S_OK;
    }

    RETURN_HR_IF(E_NOT_SUPPORTED, hasBinaryFragmentHandlers);

    HRESULT hr = m_provider.OptionsResult(options);
    RETURN_IF_FAILED(hr);

    m_options = options;
    m_optionsExplicitlySet = true;
    return hr;
}

bool WebSocket::HasBinaryMessageFragmentHandlers() const noexcept
{
    std::lock_guard<std::recursive_mutex> lock{ m_eventCallbacksMutex };
    for (auto const& pair : m_eventCallbacks)
    {
        auto observer = static_cast<HC_WEBSOCKET_OBSERVER*>(pair.second.context);
        if (observer != nullptr && observer->HasBinaryMessageFragmentEventFunction())
        {
            return true;
        }
    }

    return false;
}

void WebSocket::ClearResponseHeadersLockHeld() noexcept
{
    m_connectResponseHeaders.clear();
}

void WebSocket::ClearResponseHeaders() noexcept
{
    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };
    ClearResponseHeadersLockHeld();
}

HRESULT WebSocket::SetResponseHeaders(HttpHeaders&& headers) noexcept
try
{
    std::lock_guard<DefaultUnnamedMutex> lock{ m_stateMutex };
    m_connectResponseHeaders = std::move(headers);
    return S_OK;
}
CATCH_RETURN()

void CALLBACK WebSocket::MessageFunc(
    HCWebsocketHandle handle,
    const char* message,
    void* /*context*/
)
{
    auto& websocket{ handle->websocket };

    NotifyWebSocketRoutedHandlers(handle, true, message, nullptr, 0);

    std::unique_lock<std::recursive_mutex> lock{ websocket->m_eventCallbacksMutex };
    // Copy callbacks in case callback unregisters itself 
    auto callbacks{ websocket->m_eventCallbacks }; 

    for (auto& pair : callbacks)
    {
        try
        {
            if (pair.second.messageFunc)
            {
                pair.second.messageFunc(handle, message, pair.second.context);
            }
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketMessageFunction");
        }
    }  
}

void CALLBACK WebSocket::BinaryMessageFunc(
    HCWebsocketHandle handle,
    const uint8_t* bytes,
    uint32_t payloadSize,
    void* /*context*/
)
{
    auto& websocket{ handle->websocket };

    NotifyWebSocketRoutedHandlers(handle, true, nullptr, bytes, payloadSize);

    std::unique_lock<std::recursive_mutex> lock{ websocket->m_eventCallbacksMutex };
    auto callbacks{ websocket->m_eventCallbacks };

    for (auto& pair : callbacks)
    {
        try
        {
            if (pair.second.binaryMessageFunc)
            {
                pair.second.binaryMessageFunc(handle, bytes, payloadSize, pair.second.context);
            }
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketBinaryMessageFunction");
        }
    }
}

void CALLBACK WebSocket::BinaryMessageFragmentFunc(
    HCWebsocketHandle handle,
    const uint8_t* bytes,
    uint32_t payloadSize,
    bool isLastFragment,
    void* /*context*/
)
{
    auto& websocket{ handle->websocket };

    NotifyWebSocketRoutedHandlers(handle, true, nullptr, bytes, payloadSize);

    std::unique_lock<std::recursive_mutex> lock{ websocket->m_eventCallbacksMutex };
    auto callbacks{ websocket->m_eventCallbacks };

    for (auto& pair : callbacks)
    {
        try
        {
            if (pair.second.binaryMessageFragmentFunc)
            {
                pair.second.binaryMessageFragmentFunc(handle, bytes, payloadSize, isLastFragment, pair.second.context);
            }
            else if (pair.second.binaryMessageFunc)
            {
                HC_TRACE_INFORMATION(WEBSOCKET, "Received binary message fragment but no client handler has been set. Invoking HCWebSocketBinaryMessageFunction instead.");
                pair.second.binaryMessageFunc(handle, bytes, payloadSize, pair.second.context);
            }
        }
        catch (...)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Caught unhandled exception in HCWebSocketBinaryMessageFragmentFunction");
        }
    }
}

void CALLBACK WebSocket::CloseFunc(
    HCWebsocketHandle handle,
    HCWebSocketCloseStatus status,
    void* /*context*/
)
{
    HC_TRACE_INFORMATION(WEBSOCKET, __FUNCTION__);

    auto websocket{ handle->websocket };

    std::unique_lock<DefaultUnnamedMutex> stateLock{ websocket->m_stateMutex };
    if (!websocket->m_providerContext)
    {
        // It's possible for our websocket to get closed before we finish connecting. m_providerContext only gets populated when the connection process is 100% completed.
        // If we're still in the process of connecting, mark as disconnected and let ConnectComplete handle the cleanup.
        HC_TRACE_WARNING(WEBSOCKET, "Call to WebSocket::CloseFunc without providerContext.  This means that we're aborting the connection process unexpectedly.");
        websocket->m_state = State::Disconnected;
        return;
    }

    // We no longer expect callbacks from Provider at this point, so cleanup m_providerContext. If there are no other
    // observers of websocket, it may be destroyed now
    HC_UNIQUE_PTR<ProviderContext> reclaim{ websocket->m_providerContext };
    websocket->m_state = State::Disconnected;
    stateLock.unlock();    
    
    std::unique_lock<std::recursive_mutex> callbackLock{ websocket->m_eventCallbacksMutex };
    auto callbacks{ websocket->m_eventCallbacks };

    for (auto& pair : callbacks)
    {
        try
        {
            if (pair.second.closeFunc)
            {
                pair.second.closeFunc(handle, status, pair.second.context);
            }
        }
        catch (...)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketCloseEventFunction");
        }
    }
}

void WebSocket::NotifyWebSocketRoutedHandlers(
    _In_ HCWebsocketHandle websocket,
    _In_ bool receiving,
    _In_opt_z_ const char* message,
    _In_opt_ const uint8_t* payloadBytes,
    _In_ size_t payloadSize
)
{
    auto httpSingleton = get_http_singleton();
    if (httpSingleton)
    {
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
        for (const auto& pair : httpSingleton->m_webSocketRoutedHandlers)
        {
            pair.second.first(websocket, receiving, message, payloadBytes, payloadSize, pair.second.second);
        }
    }
}

} // namespace httpclient
} // namespace xbox

#endif
