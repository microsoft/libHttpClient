// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"

#include "compression.h"
#include <limits>

#if !HC_NOZLIB && (HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK || HC_PLATFORM == HC_PLATFORM_NINTENDO_SWITCH || HC_PLATFORM_IS_APPLE || HC_PLATFORM == HC_PLATFORM_LINUX || HC_PLATFORM == HC_PLATFORM_ANDROID || HC_PLATFORM_IS_PLAYSTATION)

#include <zlib.h>

#define CHUNK 16384
#define WINDOWBITS 15
#define GZIP_ENCODING 16

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

bool Compression::Available() noexcept
{
    return true;
}

void Compression::CompressToGzip(uint8_t* inData, size_t inDataSize, HCCompressionLevel compressionLevel, http_internal_vector<uint8_t>& outData)
{
    uint32_t compressionLevelValue = static_cast<std::underlying_type<HCCompressionLevel>::type>(compressionLevel);
    z_stream stream;

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    // deflateInit will use zlib (deflate) compression, so deflateInit2 with these flags is required for GZIP Compression
    int initResult = deflateInit2(&stream, compressionLevelValue, Z_DEFLATED, WINDOWBITS | GZIP_ENCODING, 8, Z_DEFAULT_STRATEGY);
    if (initResult != Z_OK)
    {
        return;
    }

    stream.next_in = inData;
    stream.avail_in = static_cast<uInt>(inDataSize);

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

size_t Compression::MaxDecompressedSize(size_t compressedSize) noexcept
{
    // Allow up to 100 times the compressed size, and never less than 64 MB. Real responses compress far less than
    // 100:1, while gzip itself can expand by about 1000:1.
    constexpr size_t minimumLimit = 64 * 1024 * 1024;
    constexpr size_t ratioLimit = 100;
    constexpr size_t largestLimit = (std::numeric_limits<size_t>::max)() - 1; // leaves room for the overflow check byte

    if (compressedSize > largestLimit / ratioLimit)
    {
        return largestLimit;
    }
    return (std::max)(minimumLimit, compressedSize * ratioLimit);
}

HRESULT Compression::DecompressFromGzip(uint8_t* inData, size_t inDataSize, size_t maxOutputSize, http_internal_vector<uint8_t>& outData) noexcept
{
    outData.clear();
    maxOutputSize = (std::min)(maxOutputSize, (std::numeric_limits<size_t>::max)() - 1);

    z_stream stream{};
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    // WINDOWBITS | GZIP_ENCODING - add 16 to decode only the gzip format
    int initResult = inflateInit2(&stream, WINDOWBITS | GZIP_ENCODING);
    if (initResult != Z_OK)
    {
        return initResult == Z_MEM_ERROR ? E_OUTOFMEMORY : E_FAIL;
    }

    HRESULT hr = S_OK;
    stream.next_in = inData;
    size_t inputRemaining = inDataSize;
    int ret = Z_OK;

    try
    {
        while (ret != Z_STREAM_END)
        {
            // zlib takes at most a uInt of input at a time, so feed larger input in slices.
            if (stream.avail_in == 0 && inputRemaining > 0)
            {
                uInt const slice = static_cast<uInt>((std::min<size_t>)(inputRemaining, (std::numeric_limits<uInt>::max)()));
                stream.avail_in = slice;
                inputRemaining -= slice;
            }

            // Output space is allowed to reach maxOutputSize + 1 bytes, so that any output past the limit is seen.
            if (outData.size() > maxOutputSize)
            {
                hr = E_BOUNDS;
                break;
            }
            size_t const offset = outData.size();
            size_t const chunk = (std::min<size_t>)(CHUNK, maxOutputSize + 1 - offset);
            outData.resize(offset + chunk);

            stream.next_out = outData.data() + offset;
            stream.avail_out = static_cast<uInt>(chunk);

            ret = inflate(&stream, Z_NO_FLUSH);
            outData.resize(outData.size() - stream.avail_out);

            if (ret != Z_OK && ret != Z_STREAM_END)
            {
                // Z_BUF_ERROR means no progress was possible. Every iteration supplies fresh output space and any
                // remaining input, so it can only mean the input ended before the end of the gzip stream (truncated
                // data). Retrying could never make progress, so it is treated as an error too.
                hr = ret == Z_MEM_ERROR ? E_OUTOFMEMORY : E_FAIL;
                break;
            }
        }

        if (SUCCEEDED(hr) && outData.size() > maxOutputSize)
        {
            hr = E_BOUNDS;
        }
    }
    catch (std::bad_alloc const&)
    {
        hr = E_OUTOFMEMORY;
    }

    inflateEnd(&stream);

    if (FAILED(hr))
    {
        // Don't hand back partial or corrupt data.
        outData.clear();
        outData.shrink_to_fit();
    }
    return hr;
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#else

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

bool Compression::Available() noexcept
{
    return false;
}

void Compression::CompressToGzip(uint8_t*, size_t, HCCompressionLevel, http_internal_vector<uint8_t>&)
{
    assert(false);
}

size_t Compression::MaxDecompressedSize(size_t) noexcept
{
    return 0;
}

HRESULT Compression::DecompressFromGzip(uint8_t*, size_t, size_t, http_internal_vector<uint8_t>& outData) noexcept
{
    assert(false);
    outData.clear();
    return E_NOTIMPL;
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif // !HC_NOZLIB
