#pragma once

#include "uri.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

bool getSystemProxyForUri(const Uri& inUri, Uri* outUri, String* outUsername, String* outPassword);

NAMESPACE_XBOX_HTTP_CLIENT_END
