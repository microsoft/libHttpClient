package com.xbox.httpclient;

import java.io.IOException;

import okhttp3.MediaType;
import okhttp3.RequestBody;
import okio.Buffer;
import okio.BufferedSink;
import okio.ForwardingSink;
import okio.Okio;
import okio.Sink;

public final class CountingRequestBody extends RequestBody {

    protected RequestBody delegate;
    protected Listener listener;
    protected long call;

    protected CountingSink countingSink;

    public CountingRequestBody(RequestBody delegate, Listener listener, long call) {
        this.delegate = delegate;
        this.listener = listener;
        this.call = call;
    }

    @Override
    public MediaType contentType() {
        return delegate.contentType();
    }

    @Override
    public long contentLength() {
        try {
            return delegate.contentLength();
        }
        catch (IOException e) {
            e.printStackTrace();
        }

        return -1;
    }

    @Override
    public void writeTo(BufferedSink sink) throws IOException {
        BufferedSink bufferedSink;

        countingSink = new CountingSink(sink);
        bufferedSink = Okio.buffer(countingSink);

        delegate.writeTo(bufferedSink);

        bufferedSink.flush();
    }

    protected final class CountingSink extends ForwardingSink {

        private long bytesWritten = 0;

        public CountingSink(Sink delegate) {
            super(delegate);
        }

        @Override
        public void write(Buffer source, long byteCount) throws IOException {
            super.write(source, byteCount);

            bytesWritten += byteCount;
            listener.onUploadProgress(bytesWritten, contentLength(), call);
        }

    }

    public static interface Listener {

        public void onUploadProgress(long bytesWritten, long contentLength, long call);

    }

}