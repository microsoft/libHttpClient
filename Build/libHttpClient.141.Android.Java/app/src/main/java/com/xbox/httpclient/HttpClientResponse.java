package com.xbox.httpclient;

import java.io.IOException;
import java.io.OutputStream;

import okio.Okio;
import okhttp3.Response;

class HttpClientResponse
{
    private final class NativeOutputStream extends OutputStream
    {
        private final long callHandle;

        public NativeOutputStream(long sourceCallHandle) {
            this.callHandle = sourceCallHandle;
        }

        @Override
        public void write(byte[] source) throws IOException {
            write(source, 0, source.length);
        }

        @Override
        public void write(byte[] source, int sourceOffset, int sourceLength) throws IOException {
            if (source == null) {
                throw new NullPointerException();
            }

            if (sourceOffset < 0 || sourceLength < 0 || (sourceOffset + sourceLength) > source.length) {
                throw new IndexOutOfBoundsException();
            }

            nativeWrite(this.callHandle, source, sourceOffset, sourceLength);
        }

        @Override
        public void write(int b) throws IOException {
            byte[] singleByte = { (byte)b };
            write(singleByte);
        }

        private native void nativeWrite(long callHandle, byte[] source, int sourceOffset, int sourceLength) throws IOException;

    }

    private final long callHandle;
    private final Response response;

    public HttpClientResponse(long sourceCallHandle, Response sourceResponse) {
        this.callHandle = sourceCallHandle;
        this.response = sourceResponse;
    }

    @SuppressWarnings("unused")
    public int getNumHeaders() {
        return this.response.headers().size();
    }

    @SuppressWarnings("unused")
    public String getHeaderNameAtIndex(int index) {
        if (index >= 0 && index < this.response.headers().size()) {
            return this.response.headers().name(index);
        } else {
            return null;
        }
    }

    @SuppressWarnings("unused")
    public String getHeaderValueAtIndex(int index) {
        if (index >= 0 && index < this.response.headers().size()) {
            return this.response.headers().value(index);
        } else {
            return null;
        }
    }

    @SuppressWarnings("unused")
    public void getResponseBodyBytes() {
        try {
            this.response.body().source().readAll(Okio.sink(new NativeOutputStream(callHandle)));
        } catch (IOException e) {
        } finally {
            this.response.close();
        }
    }

    @SuppressWarnings("unused")
    public int getResponseCode() {
        return response.code();
    }
}
