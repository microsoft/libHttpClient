package com.xbox.httpclient;

import java.nio.ByteBuffer;

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

        socket = OK_CLIENT.newWebSocket(request, this);
    }

    public boolean  sendMessage(String message) {
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
        onOpen();
    }

    @Override
    public void onFailure(WebSocket webSocket, Throwable t, Response response) {
        onFailure();
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
        onBinaryMessage(bytes.asByteBuffer());
    }

    public native void onOpen();
    public native void onFailure();
    public native void onClose(int code);
    public native void onMessage(String text);
    public native void onBinaryMessage(ByteBuffer data);

    protected void finalize()
    {
        socket.cancel();
    }

    private final Headers.Builder headers;
    private final long owner;

    private WebSocket socket;
}
