package com.xbox.httpclient;

import android.util.Log;

import java.io.IOException;

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
    	if ("GET".equals(method)) {
            this.requestBuilder = this.requestBuilder.method(method, null);
    	} else {
            this.requestBuilder = this.requestBuilder.method(method, RequestBody.create(MediaType.parse(contentType), body));
    	}
    }

    public void setHttpHeader(String name, String value) {
        this.requestBuilder = requestBuilder.addHeader(name, value);
    }

    public HttpClientResponse doRequest() {
        OkHttpClient client = new OkHttpClient.Builder().build();

        try {
            Response response = client.newCall(this.requestBuilder.build()).execute();
            return new HttpClientResponse(response); 
        } catch (IOException e) {
            Log.e("HttpRequestClient", "Failed to execute request", e);
            return null;
        }
    }}
