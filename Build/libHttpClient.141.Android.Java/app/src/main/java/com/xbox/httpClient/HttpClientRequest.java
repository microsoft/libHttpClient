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
        Log.i("HttpClientRequest", "ctor");
        requestBuilder = new Request.Builder();
    }

    public static HttpClientRequest createClientRequest() {
		return new HttpClientRequest();
    }

    public void setHttpUrl(String url) {
		Log.i("HttpRequestClient", "Setting url to " + url);
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
		Log.i("HttpClientRequest", "Setting header: " + name + " value: " + value);
        this.requestBuilder = requestBuilder.addHeader(name, value);
    }

    public HttpClientResponse doRequest() {
		Log.i("HttpRequestClient", "executing request");
        OkHttpClient client = new OkHttpClient.Builder().build();

        try {
            Response response = client.newCall(this.requestBuilder.build()).execute();
            Log.i("HttpRequestClient", "HTTP request succeeded");
            return new HttpClientResponse(response);
        } catch (IOException e) {
            Log.e("HttpRequestClient", "Failed to execute request");
            return new HttpClientResponse(null);
        }
    }}
