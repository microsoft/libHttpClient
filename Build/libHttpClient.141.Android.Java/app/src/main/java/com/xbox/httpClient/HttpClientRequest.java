package com.xbox.httpclient;

import android.util.Log;

import java.io.IOException;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.RequestBody;

public class HttpClientRequest {
    private Request okHttpRequest;
    private Request.Builder requestBuilder;

    public HttpClientRequest() {
        requestBuilder = new Request.Builder();
    }

    public static HttpClientRequest createClientRequest() {
        return new HttpClientRequest();
    }

    public void setHttpUrl(String url) {
        this.requestBuilder = this.requestBuilder.url(url);
    }

    public void setHttpMethodAndBody(String method, String contentType, byte[] body) {
        if (body == null || body.length == 0) {
            this.requestBuilder = this.requestBuilder.method(method, null);
        } else {
            this.requestBuilder = this.requestBuilder.method(method, RequestBody.create(MediaType.parse(contentType), body));
        }
    }

    public void setHttpHeader(String name, String value) {
        this.requestBuilder = requestBuilder.addHeader(name, value);
    }

    public void doRequestAsync(final long sourceCall) {
        OkHttpClient client = new OkHttpClient.Builder()
        		.retryOnConnectionFailure(false) // Explicitly disable retries; retry logic will be managed by native code in libHttpClient
        		.build();
        client.newCall(this.requestBuilder.build()).enqueue(new Callback() {
            @Override
            public void onFailure(final Call call, IOException e) {
                Log.e("HttpRequestClient", "Failed to execute async request", e);
                OnRequestCompleted(sourceCall, null);
            }

            @Override
            public void onResponse(Call call, final Response response) throws IOException {
                OnRequestCompleted(sourceCall, new HttpClientResponse(response));
            }
        });
    }

    private native void OnRequestCompleted(long call, HttpClientResponse response);
}
