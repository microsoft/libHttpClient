
package com.xbox.httpclient;

import android.util.Log;

import java.io.IOException;
import okhttp3.Response;

class HttpClientResponse
{
    private Response response;

    public HttpClientResponse(Response sourceResponse) {
        this.response = sourceResponse;
    }

    public int getNumHeaders() {
        return this.response.headers().size();
    }

    public String getHeaderNameAtIndex(int index) {
        if (index >= 0 && index < this.response.headers().size()) {
            return this.response.headers().name(index);
        } else {
            return null;
        }
    }

    public String getHeaderValueAtIndex(int index) {
        if (index >= 0 && index < this.response.headers().size()) {
            return this.response.headers().value(index);
        } else {
            return null;
        }
    }

    public byte[] getResponseBodyBytes() {
        try {
            byte[] responseBodyBytes = this.response.body().bytes();
            return responseBodyBytes;
        } catch (IOException e) {
            return null;
        }
    }

    public int getResponseCode() {
        return response.code();
    }
}
