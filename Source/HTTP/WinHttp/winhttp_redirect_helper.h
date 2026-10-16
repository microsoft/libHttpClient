// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "uri.h"
#include "utils.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

inline uint16_t EffectivePort(Uri const& uri) noexcept
{
    if (!uri.IsPortDefault())
    {
        return uri.Port();
    }
    return uri.IsSecure() ? 443 : 80;
}

// True if a redirect leaves the origin (scheme, host, port) of the original request.
// Credentials set by the caller must not be forwarded to a different origin.
inline bool IsCrossOriginRedirect(Uri const& original, Uri const& redirect) noexcept
{
    if (!original.IsValid() || !redirect.IsValid())
    {
        return true;
    }

    return str_icmp(original.Scheme(), redirect.Scheme()) != 0 ||
        str_icmp(original.Host(), redirect.Host()) != 0 ||
        EffectivePort(original) != EffectivePort(redirect);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
