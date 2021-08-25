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
        private final long callHandle;
        private long offset;

        public NativeInputStream(long sourceCallHandle) {
            this.callHandle = sourceCallHandle;
            this.offset = 0;
        }

        @Override
        public int read() throws IOException {
            byte[] destination = new byte[1];
            read(destination);
            return destination[0];
        }

        @Override
        public int read(byte[] b) throws IOException {
            return read(b, 0, b.length);
        }

        @Override
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

            int bytesRead = nativeRead(this.callHandle, this.offset, destination, destinationOffset, length);
            if (bytesRead == -1) {
                // Tell the OS that we are done reading
                return -1;
            }

            this.offset += bytesRead;

            return bytesRead;
        }

        @Override
        public long skip(long n) throws IOException {
            this.offset += n;
            return n;
        }

        private native int nativeRead(long callHandle, long srcOffset, byte[] dst, long dstOffset, long bytesAvailable) throws IOException;
    }

    private final long callHandle;
    private final MediaType contentType;
    private final long contentLength;

    public HttpClientRequestBody(long sourceCallHandle, String sourceContentType, long sourceContentLength) {
        this.callHandle = sourceCallHandle;
        this.contentType = sourceContentType != null ? MediaType.parse(sourceContentType) : null;
        this.contentLength = sourceContentLength;
    }

    @Override
    public MediaType contentType() {
        return contentType;
    }

    @Override
    public long contentLength() {
        return contentLength;
    }

    @Override
    public void writeTo(BufferedSink sink) throws IOException {
        sink.writeAll(Okio.source(new NativeInputStream(callHandle)));
    }
}
