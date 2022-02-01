#include "pch.h"
#include "hcwebsocket.h"

using namespace xbox::httpclient;

#define WEBSOCKET_RECVBUFFER_MAXSIZE_DEFAULT (1024 * 20)

HC_WEBSOCKET_OBSERVER::HC_WEBSOCKET_OBSERVER(std::shared_ptr<xbox::httpclient::WebSocket> websocket) : websocket{ std::move(websocket) }
{   
}

HC_WEBSOCKET_OBSERVER::~HC_WEBSOCKET_OBSERVER()
{
    HC_TRACE_VERBOSE(WEBSOCKET, __FUNCTION__);

    websocket->UnregisterEventCallbacks(m_handlerToken);
}

HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> HC_WEBSOCKET_OBSERVER::Initialize(
    _In_ std::shared_ptr<xbox::httpclient::WebSocket> websocket,
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* callbackContext
)
{
    http_stl_allocator<HC_WEBSOCKET_OBSERVER> a{};
    HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> observer{ new (a.allocate(1)) HC_WEBSOCKET_OBSERVER{ std::move(websocket) } };

    observer->m_messageFunc = messageFunc;
    observer->m_binaryMessageFunc = binaryMessageFunc;
    observer->m_binaryFragmentFunc = binaryFragmentFunc;
    observer->m_closeFunc = closeFunc;
    observer->m_callbackContext = callbackContext;
    observer->m_handlerToken = observer->websocket->RegisterEventCallbacks(messageFunc, binaryMessageFunc, binaryFragmentFunc, closeFunc, callbackContext);

    return observer;
}

void HC_WEBSOCKET_OBSERVER::SetBinaryMessageFragmentEventFunction(HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc)
{  
    websocket->UnregisterEventCallbacks(m_handlerToken);
    m_binaryFragmentFunc = binaryFragmentFunc;
    m_handlerToken = websocket->RegisterEventCallbacks(m_messageFunc, m_binaryMessageFunc, m_binaryFragmentFunc, m_closeFunc, m_callbackContext);
}

namespace xbox
{
namespace httpclient
{

WebSocket::WebSocket(uint64_t _id, WebSocketPerformInfo performInfo, HC_PERFORM_ENV* performEnv) :
    id{ _id },
    m_performInfo{ performInfo },
    m_performEnv{ performEnv }
{
}

WebSocket::~WebSocket()
{
    HC_TRACE_VERBOSE(WEBSOCKET, __FUNCTION__);
}

Result<std::shared_ptr<WebSocket>> WebSocket::Initialize()
{
    auto httpSingleton = get_http_singleton();
    RETURN_HR_IF(E_HC_NOT_INITIALISED, !httpSingleton);

    http_stl_allocator<WebSocket> a{};
    std::shared_ptr<WebSocket> websocket{ new (a.allocate(1)) WebSocket
    {
        ++httpSingleton->m_lastId,
        httpSingleton->m_websocketPerform,
        httpSingleton->m_performEnv.get()
    }, http_alloc_deleter<WebSocket>{} };

    return websocket;
}

struct WebSocket::EventCallbacks
{
    HCWebSocketMessageFunction messageFunc{ nullptr };
    HCWebSocketBinaryMessageFunction binaryMessageFunc{ nullptr };
    HCWebSocketBinaryMessageFragmentFunction binaryMessageFragmentFunc{ nullptr };
    HCWebSocketCloseEventFunction closeFunc{ nullptr };
    void* context{ nullptr };
};

uint32_t WebSocket::RegisterEventCallbacks(
    _In_opt_ HCWebSocketMessageFunction messageFunc,
    _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
    _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc,
    _In_opt_ HCWebSocketCloseEventFunction closeFunc,
    _In_opt_ void* callbackContext
)
{
    std::unique_lock<std::mutex> lock{ m_mutex };
    m_eventCallbacks[m_nextToken] = EventCallbacks{ messageFunc, binaryMessageFunc, binaryFragmentFunc, closeFunc, callbackContext };
    return m_nextToken++;
}

void WebSocket::UnregisterEventCallbacks(uint32_t registrationToken)
{
    m_eventCallbacks.erase(registrationToken);
}

// Context for ConnectAsyncProvider and WebSocket Provider Event callbacks. Ensures WebSocket lifetime throughout the ConnectAsync operation
// and if that connect operation succeeds, throughout the rest of the connection's lifetime.
struct WebSocket::ProviderContext
{
    ProviderContext(std::shared_ptr<WebSocket> websocket, XAsyncBlock* connectAsyncBlock) :
        clientConnectAsyncBlock{ connectAsyncBlock },
        internalConnectAsyncBlock{ nullptr, this, WebSocket::ConnectComplete }
    {
        observer = HC_WEBSOCKET_OBSERVER::Initialize(std::move(websocket));
    }

    ~ProviderContext()
    {
        XTaskQueueCloseHandle(internalConnectAsyncBlock.queue);
    }

    HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> observer{ nullptr };
    XAsyncBlock* const clientConnectAsyncBlock; // client owned
    XAsyncBlock internalConnectAsyncBlock;
    WebSocketCompletionResult connectResult{};
};

HRESULT WebSocket::ConnectAsync(
    _In_ http_internal_string&& uri,
    _In_ http_internal_string&& subProtocol,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, m_providerContext);

    m_uri = std::move(uri);
    m_subProtocol = std::move(subProtocol);

    auto providerContext = http_allocate_unique<ProviderContext>(shared_from_this(), asyncBlock);
    RETURN_IF_FAILED(XAsyncBegin(asyncBlock, providerContext.get(), HCWebSocketConnectAsync, nullptr, ConnectAsyncProvider));
    providerContext.release();
    return S_OK;
}

HRESULT CALLBACK WebSocket::ConnectAsyncProvider(XAsyncOp op, XAsyncProviderData const* data)
{
    ProviderContext* context{ static_cast<ProviderContext*>(data->context) };
    auto& ws{ context->observer->websocket };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        RETURN_HR_IF(E_UNEXPECTED, !ws->m_performInfo.connect);

        XTaskQueuePortHandle workPort{ nullptr };
        XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort);
        XTaskQueueCreateComposite(workPort, workPort, &context->internalConnectAsyncBlock.queue);

        try
        {
            return ws->m_performInfo.connect(ws->m_uri.data(), ws->m_subProtocol.data(), context->observer.get(), &context->internalConnectAsyncBlock, ws->m_performInfo.context, ws->m_performEnv);
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
        *result = context->connectResult;
        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        // Clean up the ProviderContext only if the connection attempt failed
        if (!ws->m_providerContext)
        {
            HC_UNIQUE_PTR<ProviderContext> reclaim{ context };
        }
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
    ProviderContext* context{ static_cast<ProviderContext*>(async->context) };

    HRESULT hr = HCGetWebSocketConnectResult(&context->internalConnectAsyncBlock, &context->connectResult);
    if (SUCCEEDED(hr) && SUCCEEDED(context->connectResult.errorCode))
    {
        // Connect was sucessful. Store ProviderContext until the connection is closed. It will be reclaimed in WebSocket::CloseFunc
        context->observer->websocket->m_providerContext = context;
    }

    XAsyncComplete(context->clientConnectAsyncBlock, hr, sizeof(WebSocketCompletionResult));
}

HRESULT WebSocket::SendAsync(
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, !m_providerContext);
    RETURN_HR_IF(E_UNEXPECTED, !m_performInfo.sendText);

    try
    {
        NotifyWebSocketRoutedHandlers(m_providerContext->observer.get(), false, message, nullptr, 0);
        return m_performInfo.sendText(m_providerContext->observer.get(), message, asyncBlock, m_performInfo.context);
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
    RETURN_HR_IF(E_UNEXPECTED, !m_providerContext);
    RETURN_HR_IF(E_UNEXPECTED, !m_performInfo.sendBinary);

    try
    {
        NotifyWebSocketRoutedHandlers(m_providerContext->observer.get(), false, nullptr, payloadBytes, payloadSize);
        return m_performInfo.sendBinary(m_providerContext->observer.get(), payloadBytes, payloadSize, asyncBlock, m_performInfo.context);
    }
    catch (...)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketSendBinaryMessageFunction [ID %llu]", TO_ULL(id));
        return E_FAIL;
    }
}

HRESULT WebSocket::Disconnect()
{
    RETURN_HR_IF(E_UNEXPECTED, !m_providerContext);
    RETURN_HR_IF(E_UNEXPECTED, !m_performInfo.disconnect);

    try
    {
        return m_performInfo.disconnect(m_providerContext->observer.get(), HCWebSocketCloseStatus::Normal, m_performInfo.context);
    }
    catch (...)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Caught unhandled exception in HCWebSocketDisconnectFunction [ID %llu]", TO_ULL(id));
        return E_FAIL;
    }
}

const HttpHeaders& WebSocket::Headers() const noexcept
{
    return m_connectHeaders;
}

const http_internal_string& WebSocket::ProxyUri() const noexcept
{
    return m_proxyUri;
}

const bool WebSocket::ProxyDecryptsHttps() const noexcept
{
    return m_allowProxyToDecryptHttps;
}

size_t WebSocket::MaxReceiveBufferSize() const noexcept
{
    return m_maxReceiveBufferSize;
}

HRESULT WebSocket::SetHeader(
    http_internal_string&& headerName,
    http_internal_string&& headerValue
) noexcept
{
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_providerContext);
    m_connectHeaders[headerName] = headerValue;
    return S_OK;
}

HRESULT WebSocket::SetProxyUri(
    http_internal_string&& proxyUri
) noexcept
{
    RETURN_HR_IF(E_HC_CONNECT_ALREADY_CALLED, m_providerContext);
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
    m_allowProxyToDecryptHttps = allowProxyToDecryptHttps;
    return S_OK;
}

HRESULT WebSocket::SetMaxReceiveBufferSize(size_t maxReceiveBufferSizeBytes) noexcept
{
    m_maxReceiveBufferSize = maxReceiveBufferSizeBytes;
    return S_OK;
}

void CALLBACK WebSocket::MessageFunc(
    HCWebsocketHandle handle,
    const char* message,
    void* /*context*/
)
{
    auto& websocket{ handle->websocket };

    NotifyWebSocketRoutedHandlers(handle, true, message, nullptr, 0);

    std::unique_lock<std::mutex> lock{ websocket->m_mutex };
    auto callbacks{ websocket->m_eventCallbacks };
    lock.unlock();

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

    std::unique_lock<std::mutex> lock{ websocket->m_mutex };
    auto callbacks{ websocket->m_eventCallbacks };
    lock.unlock();

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

    std::unique_lock<std::mutex> lock{ websocket->m_mutex };
    auto callbacks{ websocket->m_eventCallbacks };
    lock.unlock();

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
    HC_TRACE_VERBOSE(WEBSOCKET, __FUNCTION__);

    auto& websocket{ handle->websocket };

    if (!websocket->m_providerContext)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Unexpected call to WebSocket::CloseFunc will be ignored!");
        return;
    }

    std::unique_lock<std::mutex> lock{ websocket->m_mutex };
    auto callbacks{ websocket->m_eventCallbacks };
    lock.unlock();

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

    // We no longer expect callbacks from Provider at this point, so cleanup m_providerContext. If there are no other
    // observers of websocket, it may be destroyed here
    HC_UNIQUE_PTR<ProviderContext> reclaim{ websocket->m_providerContext };
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
