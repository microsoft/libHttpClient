package com.xbox.httpclient;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Build;

import org.jetbrains.annotations.NotNull;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.net.ConnectException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;

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

    private final Context appContext;
    private Request.Builder requestBuilder;

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
                boolean isNoNetworkFailure =
                    e instanceof UnknownHostException ||
                    e instanceof ConnectException ||
                    e instanceof SocketTimeoutException;

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
        ConnectivityManager cm = (ConnectivityManager)appContext.getSystemService(Context.CONNECTIVITY_SERVICE);

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return "API version too old - no network info!";
        }

        StringBuilder builder = new StringBuilder();

        builder
            .append("Default proxy: ")
            .append(cm.getDefaultProxy() != null)
            .append('\n');

        Network activeNetwork = cm.getActiveNetwork();

        builder
            .append("Has active network: ")
            .append(activeNetwork != null)
            .append('\n');

        Network[] allNetworks = cm.getAllNetworks();

        for (Network network : allNetworks) {
            String networkDetails = GetDetailsForNetwork(cm, network, network.equals(activeNetwork));

            builder
                .append(networkDetails)
                .append('\n');
        }

        return builder.toString();
    }

    private String GetDetailsForNetwork(ConnectivityManager cm, Network network, boolean isActiveNetwork) {
        NetworkDetails networkDetails = new NetworkDetails();

        networkDetails.addSection("isActiveNetwork", isActiveNetwork);

        LinkProperties linkProperties = cm.getLinkProperties(network);

        if (linkProperties != null) {
            networkDetails.addSection("hasProxy", linkProperties.getHttpProxy() != null);
        } else {
            networkDetails.addSection("hasLinkProperties", false);
        }

        NetworkCapabilities networkCapabilities = cm.getNetworkCapabilities(network);

        if (networkCapabilities != null) {
            networkDetails.addSection("isWifi", networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI));
            networkDetails.addSection("isBluetooth", networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_BLUETOOTH));
            networkDetails.addSection("isCellular", networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR));
            networkDetails.addSection("isVpn", networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN));
            networkDetails.addSection("isEthernet", networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET));

            networkDetails.addSection("shouldHaveInternet", networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET));
            networkDetails.addSection("isNotVpn", networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN));

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                networkDetails.addSection("internetWasValidated", networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED));
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                networkDetails.addSection("isNotSuspended", networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED));
            }
        } else {
            networkDetails.addSection("hasNetworkCapabilities", false);
        }

        return networkDetails.toString();
    }

    private native void OnRequestCompleted(long call, HttpClientResponse response);
    private native void OnRequestFailed(
        long call,
        String errorMessage,
        String stackTrace,
        String networkDetails,
        boolean isNoNetwork
    );

    private static class NetworkDetails {
        private final List<String> sections = new ArrayList<>();

        void addSection(String key, boolean value) {
            sections.add(key + ": " + value);
        }

        @NotNull
        public String toString() {
            StringBuilder builder = new StringBuilder();
            builder.append("Network details: ");

            // String.join() is only available in API 26+ *rolls-eyes*
            for (int i = 0; i < sections.size(); i++) {
                if (i > 0) {
                    builder.append(", ");
                }

                builder.append(sections.get(i));
            }

            return builder.toString();
        }
    }
}
