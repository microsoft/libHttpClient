// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
#pragma once

#include "utils.h"
#include <zlib.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class Compression
{
public:
    static void CompressToGzip(http_internal_vector<uint8_t> in_data, uInt in_data_size, http_internal_vector<uint8_t>& out_data);

private:
    Compression();
};

NAMESPACE_XBOX_HTTP_CLIENT_END
#endif