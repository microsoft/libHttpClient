// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"

#if !HC_NOZLIB
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK

#include "compression.h"

#define CHUNK 16384
#define WINDOWBITS 15
#define GZIP_ENCODING 16

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

Compression::Compression()
{
}

void Compression::CompressToGzip(uint8_t* inData, uInt inDataSize, HCCompressionLevel compressionLevel, http_internal_vector<uint8_t>& outData)
{
    uint32_t compressionLevelValue = static_cast<std::underlying_type<HCCompressionLevel>::type>(compressionLevel);
    uint8_t buffer[CHUNK];
    z_stream strm;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    deflateInit2(&strm, compressionLevelValue, Z_DEFLATED, WINDOWBITS | GZIP_ENCODING, 8, Z_DEFAULT_STRATEGY);

    strm.next_in = inData;
    strm.avail_in = inDataSize;

    do
    {
        int have;
        strm.avail_out = CHUNK;
        strm.next_out = buffer;
        deflate(&strm, Z_FINISH);
        have = CHUNK - strm.avail_out;
        outData.insert(outData.end(), buffer, buffer + have);
    } 
    while (strm.avail_out == 0);

    deflateEnd(&strm);
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif
#endif // !HC_NOZLIB