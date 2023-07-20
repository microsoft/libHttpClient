#include "pch.h"
#include "IHttpProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT IHttpProvider::SetGlobalProxy(String const& /*proxyUri*/) noexcept
{
    return E_NOTIMPL;
}

HRESULT IHttpProvider::CleanupAsync(XAsyncBlock* async) noexcept
{
    return XAsyncBegin(async, nullptr, nullptr, XASYNC_IDENTITY(IHttpProvider::CleanupAsync), [](XAsyncOp op, XAsyncProviderData const* data)
    {
        switch (op)
        {
        case XAsyncOp::Begin:
        {
            // synchronously complete
            XAsyncComplete(data->async, S_OK, 0);
            return S_OK;
        }
        default:
        {
            return S_OK;
        }
        }
    });
}

NAMESPACE_XBOX_HTTP_CLIENT_END
