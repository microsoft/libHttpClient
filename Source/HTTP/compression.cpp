// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"

#if !HC_NOZLIB
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK || HC_PLATFORM == HC_PLATFORM_NINTENDO_SWITCH || HC_PLATFORM == HC_PLATFORM_IOS || HC_PLATFORM == HC_PLATFORM_MAC

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
    z_stream stream;

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    // deflateInit will use zlib (deflate) compression, so deflateInit2 with these flags is required for GZIP Compression
    deflateInit2(&stream, compressionLevelValue, Z_DEFLATED, WINDOWBITS | GZIP_ENCODING, 8, Z_DEFAULT_STRATEGY);

    stream.next_in = inData;
    stream.avail_in = inDataSize;

    // Initialize output buffer with CHUNK size so first iteration of deflate will have enough room to allocate compressed bytes.
    outData = http_internal_vector<uint8_t>(CHUNK);
    uint8_t* bufferPtr;
    size_t index = 0;

    do
    {
        // bufferPtr is updated to point the next available byte in the output buffer so zlib will write the next chunk of compressed bytes from that position onwards
        bufferPtr = &outData.at(index);

        stream.avail_out = CHUNK;
        stream.next_out = bufferPtr;
        deflate(&stream, Z_FINISH);

        // The value of avail_out after deflate will tell us how many bytes were unused in compression after deflate.
        // A value of 0 means that all bytes available for compression were used so we need to keep iterating.
        if (stream.avail_out == 0)
        {
            index = outData.size();
            outData.resize(outData.size() + CHUNK);
        }
        // A non-zero value will indicate that there were some bytes left, which means that compression ended and we shrink the vector to its right size based on the leftover bytes.
        else
        {
            outData.resize(outData.size() - stream.avail_out);
        }
    } 
    while (stream.avail_out == 0);

    deflateEnd(&stream);
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif
#endif // !HC_NOZLIB
