
package com.xbox.httpclient;

import android.util.Log;

import java.io.IOException;
import okhttp3.Response;

class HttpClientResponse
{
    private Response okHttpResponse;

    public HttpClientResponse(Response sourceResponse) {
        this.okHttpResponse = sourceResponse;
    }

	public boolean succeeded() {
        return this.okHttpResponse.isSuccessful();
	}

	public int getNumHeaders() {
        return this.okHttpResponse.headers().size();
	}

    public String getHeaderNameAtIndex(int index) {
		if (index >= 0 && index < this.okHttpResponse.headers().size()) {
            return this.okHttpResponse.headers().name(index);
		} else {
            return null;
		}
	}

	public String getHeaderValueAtIndex(int index) {
		if (index >= 0 && index < this.okHttpResponse.headers().size()) {
            return this.okHttpResponse.headers().value(index);
		} else {
            return null;
		}
	}

	public String getResponseString() {
	    try {
            return this.okHttpResponse.body().string();
		} catch (IOException e) {
            return null;
		}
	}

	public byte[] getResponseBodyBytes() {
		try {
			byte[] responseBodyBytes = this.okHttpResponse.body().bytes();
            return responseBodyBytes;

		} catch (IOException e) {
            return null;
		}
	}

	public int getResponseCode() {
        return okHttpResponse.code();
	}
}
