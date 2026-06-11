package com.xbox.httpclient;

import java.nio.ByteBuffer;
import java.util.concurrent.TimeUnit;

import okhttp3.Headers;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;

public final class HttpClientWebSocket extends WebSocketListener {
    private final static OkHttpClient OK_CLIENT;

    static {
        OK_CLIENT = new OkHttpClient();
    }

    HttpClientWebSocket(long owner) {
        this.headers = new Headers.Builder();
        this.owner = owner;
        this.pingInterval = 0;
    }

    public void setPingInterval(long pingInterval) {
        this.pingInterval = pingInterval;
    }

    public void addHeader(String name, String value) {
        headers.add(name, value);
    }

    public void connect(String url, String subProtocol) {
        addHeader("Sec-WebSocket-Protocol", subProtocol);

        Request request = new Request.Builder()
            .url(url)
            .headers(headers.build())
            .build();

        OkHttpClient clientWithPing = OK_CLIENT.newBuilder()
                .pingInterval(pingInterval, TimeUnit.SECONDS) // default is 0, which disables pings
                .build();

        socket = clientWithPing.newWebSocket(request, this);
    }

    public boolean sendMessage(String message) {
        return socket.send(message);
    }

    public boolean sendBinaryMessage(java.nio.ByteBuffer message) {
       return socket.send(okio.ByteString.of(message));
    }

    public void disconnect(int closeStatus) {
        socket.close(closeStatus, null);
    }

    @Override
    public void onOpen(WebSocket webSocket, Response response) {
        Headers responseHeaders = response != null ? response.headers() : null;
        onOpen(getHeaderNames(responseHeaders), getHeaderValues(responseHeaders));
    }

    @Override
    public void onFailure(WebSocket webSocket, Throwable t, Response response) {
        Headers responseHeaders = response != null ? response.headers() : null;
        onFailure(
            response != null ? response.code() : -1,
            getHeaderNames(responseHeaders),
            getHeaderValues(responseHeaders));
    }

    @Override
    public void onClosing(WebSocket webSocket, int code, String reason) {}

    @Override
    public void onClosed(WebSocket webSocket, int code, String reason) {
        onClose(code);
    }

    @Override
    public void onMessage(WebSocket webSocket, String text) {
        onMessage(text);
    }

    @Override
    public void onMessage(WebSocket webSocket, okio.ByteString bytes) {
        // These needs to be a directly allocated ByteBuffer or
        // native layer won't be able to access this.

        ByteBuffer buffer = ByteBuffer.allocateDirect(bytes.size());
        buffer.put(bytes.toByteArray());
        buffer.position(0);

        onBinaryMessage(buffer);
    }

    public native void onOpen(String[] headerNames, String[] headerValues);
    public native void onFailure(int statusCode, String[] headerNames, String[] headerValues);
    public native void onClose(int code);
    public native void onMessage(String text);
    public native void onBinaryMessage(ByteBuffer data);

    protected void finalize()
    {
        socket.cancel();
    }

    private final Headers.Builder headers;
    private final long owner;
    private long pingInterval;

    private WebSocket socket;

    private static String[] getHeaderNames(Headers headers) {
        if (headers == null) {
            return null;
        }

        String[] names = new String[headers.size()];
        for (int i = 0; i < headers.size(); ++i) {
            names[i] = headers.name(i);
        }
        return names;
    }

    private static String[] getHeaderValues(Headers headers) {
        if (headers == null) {
            return null;
        }

        String[] values = new String[headers.size()];
        for (int i = 0; i < headers.size(); ++i) {
            values[i] = headers.value(i);
        }
        return values;
    }
}
