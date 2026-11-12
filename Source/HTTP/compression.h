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

    // Largest decompressed size accepted for a gzip response of compressedSize bytes.
    static size_t MaxDecompressedSize(size_t compressedSize) noexcept;

    // Decodes the first gzip member in the input; any bytes after the end of that member are ignored.
    // Fails with E_BOUNDS if the decompressed data would exceed maxOutputSize bytes, and with E_FAIL if the input
    // does not start with one complete, valid gzip member. outData is empty on failure.
    static HRESULT DecompressFromGzip(uint8_t* inData, size_t inDataSize, size_t maxOutputSize, http_internal_vector<uint8_t>& outData) noexcept;

private:
    Compression() = delete;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
