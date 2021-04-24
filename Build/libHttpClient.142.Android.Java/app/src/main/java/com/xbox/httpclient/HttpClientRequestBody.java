package com.xbox.httpclient;

import java.io.InputStream;
import java.io.IOException;

import okio.BufferedSink;
import okio.Okio;

import okhttp3.MediaType;
import okhttp3.RequestBody;

public final class HttpClientRequestBody extends RequestBody
{
    private final class NativeInputStream extends InputStream
    {
        final long callHandle;
        long offset;

        public NativeInputStream(long sourceCallHandle) {
            callHandle = sourceCallHandle;
            offset = 0;
        }

        public int read() throws IOException {
            byte[] destination = new byte[1];
            read(destination);
            return destination[0];
        }

        public int read(byte[] b) throws IOException {
            return read(b, 0, b.length);
        }

        public int read(byte[] destination, int destinationOffset, int length) throws IOException {
            if (destination == null) {
                throw new NullPointerException();
            }

            if (destinationOffset < 0 || length < 0 || (destinationOffset + length) > destination.length) {
                throw new IndexOutOfBoundsException();
            }

            if (length == 0) {
                return 0;
            }

            int bytesRead = nativeRead(callHandle, offset, destination, destinationOffset, length);
            if (bytesRead == 0) {
                return -1;
            }

            offset += bytesRead;

            return bytesRead;
        }

        public long skip(long n) throws IOException {
            offset += n;
            return n;
        }

        private native int nativeRead(long callHandle, long srcOffset, byte[] dst, long dstOffset, long bytesAvailable) throws IOException;
    }

    long callHandle;
    MediaType contentType;
    long contentLength;

    public HttpClientRequestBody(long sourceCallHandle, String sourceContentType, long sourceContentLength) {
        callHandle = sourceCallHandle;
        if (contentType != null) {
            contentType = MediaType.parse(sourceContentType);
        }
        contentLength = sourceContentLength;
    }

    public MediaType contentType() {
        return contentType;
    }

    public long contentLength() {
        return contentLength;
    }

    public void writeTo(BufferedSink sink) throws IOException {
        sink.writeAll(Okio.source(new NativeInputStream(callHandle)));
    }
}
