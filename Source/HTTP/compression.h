// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in
#pragma once

#include "utils.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class Compression
{
public:
    static bool Available() noexcept;

    static void CompressToGzip(uint8_t* inData, size_t inDataSize, HCCompressionLevel compressionLevel, http_internal_vector<uint8_t>& outData);
    static void DecompressFromGzip(uint8_t* inData, size_t inDataSize, http_internal_vector<uint8_t>& outData);

private:
    Compression() = delete;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
