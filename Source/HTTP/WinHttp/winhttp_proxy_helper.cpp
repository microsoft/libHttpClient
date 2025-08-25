#include "pch.h"
#include "winhttp_provider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

http_internal_wstring WinHttpProvider::BuildNamedProxyString(_In_ const xbox::httpclient::Uri &proxyUri)
{
    http_internal_wstring wProxyHost = utf16_from_utf8(proxyUri.Host());
    if (proxyUri.IsPortDefault())
    {
        return wProxyHost;
    }
    if (proxyUri.Port() > 0)
    {
        // uint16_t promotes safely; explicit cast not required
        auto portStr = std::to_wstring(proxyUri.Port());
        http_internal_wstring result = wProxyHost;
        result.push_back(L':');
        result.append(portStr.c_str());
        return result;
    }
    return wProxyHost;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
