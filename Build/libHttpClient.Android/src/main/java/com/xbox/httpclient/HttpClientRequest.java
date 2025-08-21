package com.xbox.httpclient;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.os.Build;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.UnknownHostException;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.Interceptor;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.RequestBody;
import okhttp3.ResponseBody;
import okio.Buffer;
import okio.BufferedSource;
import okio.ForwardingSource;
import okio.Okio;
import okio.Source;

public class HttpClientRequest {
    private static final OkHttpClient OK_CLIENT;
    private static final byte[] NO_BODY = new byte[0];

    private final Context appContext;
    private Request.Builder requestBuilder;

    private static final ProgressListener downloadProgressListener = new ProgressListener() {
        @Override 
        public void onDownloadProgress(long bytesRead, long contentLength, boolean done, long call) {
            ReportProgress(call, bytesRead, contentLength, false);
        }
    };

    private static final CountingRequestBody.Listener uploadProgressListener = new CountingRequestBody.Listener() {
        @Override
        public void onUploadProgress(long bytesWritten, long contentLength, long call) {
            ReportProgress(call, bytesWritten, contentLength, true);
        }
    };

    static {
        OK_CLIENT = new OkHttpClient.Builder()
                    .retryOnConnectionFailure(false) // Explicitly disable retries; retry logic will be managed by native code in libHttpClient
                    .build();
    }

    public HttpClientRequest(Context appContext) {
        this.appContext = appContext;
        this.requestBuilder = new Request.Builder();
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

                this.requestBuilder.method(method, requestBody);
            }
        } else {
            requestBody = new HttpClientRequestBody(call, contentType, contentLength);

            // Decorate the request body to keep track of the upload progress
            CountingRequestBody countingBody = new CountingRequestBody(requestBody, uploadProgressListener, call);

            this.requestBuilder.method(method, countingBody);
        }
    }

    @SuppressWarnings("unused")
    public void setHttpHeader(String name, String value) {
        this.requestBuilder = requestBuilder.addHeader(name, value);
    }

    @SuppressWarnings("unused")
    public void doRequestAsync(final long sourceCall) {
        OkHttpClient interceptorClient = OK_CLIENT.newBuilder()
                .addNetworkInterceptor(new Interceptor() {
                    @Override
                    public Response intercept(Chain chain) throws IOException {
                        Response originalResponse = chain.proceed(chain.request());
                        return originalResponse.newBuilder()
                                .body(new ProgressResponseBody(originalResponse.body(), downloadProgressListener, sourceCall))
                                .build();
                    }
                })
                .build();

        interceptorClient.newCall(this.requestBuilder.build()).enqueue(new Callback() {
            @Override
            public void onFailure(final Call call, IOException e) {
                // isNoNetworkFailure indicates to the native code when to assume the client is
                // disconnected from the internet. In no network cases, retry logic will not be
                // activated.
                boolean isNoNetworkFailure = e instanceof UnknownHostException;

                StringWriter sw = new StringWriter();
                PrintWriter pw = new PrintWriter(sw);
                e.printStackTrace(pw);

                OnRequestFailed(
                    sourceCall,
                    e.getClass().getCanonicalName(),
                    sw.toString(),
                    GetAllNetworksInfo(),
                    isNoNetworkFailure
                );
            }

            @Override
            public void onResponse(Call call, final Response response) {
                OnRequestCompleted(sourceCall, new HttpClientResponse(sourceCall, response));
            }
        });
    }

    private String GetAllNetworksInfo() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return "API version too old - unable to get network info!";
        }

        ConnectivityManager cm = (ConnectivityManager)appContext.getSystemService(Context.CONNECTIVITY_SERVICE);

        StringBuilder builder = new StringBuilder();

        builder
            .append("Has default proxy: ")
            .append(cm.getDefaultProxy() != null)
            .append('\n');

        builder
            .append("Has active network: ")
            .append(cm.getActiveNetwork() != null)
            .append('\n');

        Network[] allNetworks = cm.getAllNetworks();

        for (int i = 0; i < allNetworks.length; i++) {
            if (i > 0) {
                builder.append("\n");
            }

            builder.append(NetworkObserver.NetworkDetails.getNetworkDetails(allNetworks[i], cm));
        }

        return builder.toString();
    }

    private native void OnRequestCompleted(long call, HttpClientResponse response);
    private native void OnRequestFailed(
        long call,
        String errorMessage,
        String stackTrace,
        String networkDetails,
        boolean isNoNetwork
    );
    private static native void ReportProgress(
            long call,
            long current,
            long total,
            boolean isUpload
    );

    private static class ProgressResponseBody extends ResponseBody {

        private final ResponseBody responseBody;
        private final ProgressListener progressListener;
        private final long call;
        private BufferedSource bufferedSource;

        ProgressResponseBody(ResponseBody responseBody, ProgressListener progressListener, long call) {
            this.responseBody = responseBody;
            this.progressListener = progressListener;
            this.call = call;
        }

        @Override public MediaType contentType() {
            return responseBody.contentType();
        }

        @Override public long contentLength() {
            return responseBody.contentLength();
        }

        @Override public BufferedSource source() {
            if (bufferedSource == null) {
                bufferedSource = Okio.buffer(source(responseBody.source()));
            }
            return bufferedSource;
        }

        private Source source(Source source) {
            return new ForwardingSource(source) {
                long totalBytesRead = 0L;

                @Override public long read(Buffer sink, long byteCount) throws IOException {
                    long bytesRead = super.read(sink, byteCount);
                    // read() returns the number of bytes read, or -1 if this source is exhausted.
                    totalBytesRead += bytesRead != -1 ? bytesRead : 0;
                    progressListener.onDownloadProgress(totalBytesRead, responseBody.contentLength(), bytesRead == -1, call);
                    return bytesRead;
                }
            };
        }
    }

    interface ProgressListener {
        void onDownloadProgress(long bytesRead, long contentLength, boolean done, long call);
    }
}