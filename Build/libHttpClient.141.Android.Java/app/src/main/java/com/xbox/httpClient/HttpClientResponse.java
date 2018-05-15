
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
		if (this.okHttpResponse != null) {
            return this.okHttpResponse.isSuccessful();
		}
		else {
            return false;
		}
	}

	public int getNumHeaders() {
		if (this.okHttpResponse != null) {
            return this.okHttpResponse.headers().size();
		} else {
            return 0;
		}
	}

    public String getHeaderNameAtIndex(int index) {
		if (this.okHttpResponse != null && index >= 0 && index < this.okHttpResponse.headers().size()) {
            return this.okHttpResponse.headers().name(index);
		} else {
            return null;
		}
	}


	public String getHeaderValueAtIndex(int index) {
		if (this.okHttpResponse != null && index >= 0 && index < this.okHttpResponse.headers().size()) {
            return this.okHttpResponse.headers().value(index);
		} else {
            return null;
		}
	}

	public String getResponseString() {
		if (this.okHttpResponse != null) {
			try {
            return this.okHttpResponse.body().string();
			} catch (IOException e) {
                return "";
			}
		} else {
            return "";
		}
	}

	public byte[] getResponseBodyBytes() {
		if (this.okHttpResponse != null) {
			try {
			    byte[] responseBodyBytes = this.okHttpResponse.body().bytes();
                return responseBodyBytes;

			} catch (IOException e) {
                return null;
			}
		}

        return null;
	}

	public long getResponseBodyBytesSize() {
		if (this.okHttpResponse != null) {
//			try {
                return this.okHttpResponse.body().contentLength();

//				byte[] responseBodyBytes = this.okHttpResponse.body().bytes();
//			    Log.i("HttpResponseClient", "Returning response length: " + responseBodyBytes.length);
			    //return responseBodyBytes.length;
//			} catch (IOException e) {
//                return 0;
//			}
		} else {
			Log.i("HttpResponseClient", "Error fetching response size");
			return 0;
		}
	}

	public int getResponseCode() {
		if (this.okHttpResponse != null) {
            return okHttpResponse.code();
		} else {
            return 500;
		}
	}
}
