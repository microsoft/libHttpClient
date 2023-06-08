#pragma once

#include <httpClient/httpClient.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Internal interface for an Http Provider. Used as a base class for any in-box Http implementations.
class IHttpProvider
{
public:
    IHttpProvider() = default;
    IHttpProvider(IHttpProvider const&) = delete;
    IHttpProvider& operator=(IHttpProvider const&) = delete;
    virtual ~IHttpProvider() = default;

    // For legacy reasons, implementations of PerformAsync should not implement a full XAsyncProvider. Instead,
    // they should perform the HTTP request, set the result via the public httpProvider.h APIs, and then mark the
    // operation as complete by calling XAsyncComplete.
    virtual HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept = 0;

    // Set a global proxy address for all HTTP calls. Default implementation returns E_NOTIMPL.
    virtual HRESULT SetGlobalProxy(
        String const& proxyUri
    ) noexcept;

    // Method to cleanup any asynchronous background work that the provider may have started. This method shouldn't
    // cleanup or await a specific PerformAsync operation, just any global background work.
    // Because this will not be required for all providers, a default "no-op" implementation exists.
    virtual HRESULT CleanupAsync(
        XAsyncBlock* async
    ) noexcept;
};

NAMESPACE_XBOX_HTTP_CLIENT_END