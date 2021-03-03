package com.xbox.httpclient;

import java.io.IOException;
import okhttp3.Response;

class HttpClientResponse
{
    private final Response response;

    public HttpClientResponse(Response sourceResponse) {
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
    public byte[] getResponseBodyBytes() {
        try {
            byte[] responseBodyBytes = this.response.body().bytes();
            return responseBodyBytes;
        } catch (IOException e) {
            return null;
        }
    }

    @SuppressWarnings("unused")
    public int getResponseCode() {
        return response.code();
    }
}
