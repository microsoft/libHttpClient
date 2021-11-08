package com.xbox.httpclient;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.UnknownHostException;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.RequestBody;

public class HttpClientRequest {
    private static final OkHttpClient OK_CLIENT;
    private static final byte[] NO_BODY = new byte[0];

    private Request.Builder requestBuilder;

    static {
        OK_CLIENT = new OkHttpClient.Builder()
                .retryOnConnectionFailure(false) // Explicitly disable retries; retry logic will be managed by native code in libHttpClient
                .build();
    }

    public HttpClientRequest() {
        requestBuilder = new Request.Builder();
    }

    @SuppressWarnings("unused")
    public void setHttpUrl(String url) {
        this.requestBuilder = this.requestBuilder.url(url);
    }

    @SuppressWarnings("unused")
    public void setHttpMethodAndBody(String method, long call, String contentType, long contentLength) {
        RequestBody requestBody = null;
        if (contentLength == 0) {
            if ("POST".equals(method) || "PUT".equals(method)) {
                MediaType mediaType = (contentType != null ? MediaType.parse(contentType) : null);
                requestBody = RequestBody.create(NO_BODY, mediaType);
            }
        } else {
            requestBody = new HttpClientRequestBody(call, contentType, contentLength);
        }
        this.requestBuilder.method(method, requestBody);
    }

    @SuppressWarnings("unused")
    public void setHttpHeader(String name, String value) {
        this.requestBuilder = requestBuilder.addHeader(name, value);
    }

    @SuppressWarnings("unused")
    public void doRequestAsync(final long sourceCall) {
        OK_CLIENT.newCall(this.requestBuilder.build()).enqueue(new Callback() {
            @Override
            public void onFailure(final Call call, IOException e) {
                boolean isNoNetworkFailure = e instanceof UnknownHostException;

                StringWriter sw = new StringWriter();
                PrintWriter pw = new PrintWriter(sw);
                e.printStackTrace(pw);

                OnRequestFailed(sourceCall, e.getClass().getCanonicalName(), sw.toString(), isNoNetworkFailure);
            }

            @Override
            public void onResponse(Call call, final Response response) {
                OnRequestCompleted(sourceCall, new HttpClientResponse(sourceCall, response));
            }
        });
    }

    private native void OnRequestCompleted(long call, HttpClientResponse response);
    private native void OnRequestFailed(long call, String errorMessage, String stackTrace, boolean isNoNetwork);
}
