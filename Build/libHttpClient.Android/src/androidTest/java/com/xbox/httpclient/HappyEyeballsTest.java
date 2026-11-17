// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
package com.xbox.httpclient;

import org.junit.Test;

import java.io.IOException;
import java.lang.reflect.Field;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import javax.net.SocketFactory;

import okhttp3.Dns;
import okhttp3.Dispatcher;
import okhttp3.ConnectionPool;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;

import static org.junit.Assert.*;

/** Verifies OkHttp's default fast fallback on the unchanged production clients.
 * Uses a deterministic IPv6 black hole to exercise native IPv4 fallback.
 * Runs as instrumentation or as a host JUnit test with the Android API jar.
 */
public class HappyEyeballsTest {
    private static OkHttpClient productionClient(Class<?> provider) throws Exception {
        Field field = provider.getDeclaredField("OK_CLIENT");
        field.setAccessible(true);
        return (OkHttpClient) field.get(null);
    }

    private static final class BlackHoleSocketFactory extends SocketFactory {
        final CountDownLatch ipv6Started = new CountDownLatch(1);
        final CountDownLatch ipv6Closed = new CountDownLatch(1);

        @Override public Socket createSocket() {
            return new Socket() {
                private boolean ipv6;
                @Override public void connect(SocketAddress endpoint, int timeout) throws IOException {
                    if (!(((InetSocketAddress) endpoint).getAddress() instanceof Inet6Address)) {
                        super.connect(endpoint, timeout);
                        return;
                    }
                    ipv6 = true;
                    ipv6Started.countDown();
                    // The IPv6 attempt can only finish by timing out or being closed.
                    // A working fast fallback must finish HTTP before this timeout.
                    synchronized (this) {
                        long until = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeout);
                        while (!isClosed() && System.nanoTime() < until) {
                            try { wait(50); }
                            catch (InterruptedException e) {
                                Thread.currentThread().interrupt();
                                throw new SocketException("Interrupted");
                            }
                        }
                    }
                    throw new SocketException("IPv6 black hole closed or timed out");
                }
                @Override public synchronized void close() throws IOException {
                    super.close();
                    notifyAll();
                    if (ipv6) ipv6Closed.countDown();
                }
            };
        }
        @Override public Socket createSocket(String h, int p) { throw new UnsupportedOperationException(); }
        @Override public Socket createSocket(String h, int p, InetAddress l, int lp) { throw new UnsupportedOperationException(); }
        @Override public Socket createSocket(InetAddress h, int p) { throw new UnsupportedOperationException(); }
        @Override public Socket createSocket(InetAddress h, int p, InetAddress l, int lp) { throw new UnsupportedOperationException(); }
    }

    private OkHttpClient blackHoleClient(Class<?> provider, BlackHoleSocketFactory sockets) throws Exception {
        OkHttpClient client = productionClient(provider);
        assertTrue("OkHttp should enable fast fallback by default", client.fastFallback());
        return client.newBuilder()
                .dispatcher(new Dispatcher())
                .connectionPool(new ConnectionPool())
                .proxy(Proxy.NO_PROXY)
                .dns(new Dns() {
                    @Override public java.util.List<InetAddress> lookup(String hostname) throws java.net.UnknownHostException {
                        return Arrays.asList(InetAddress.getByName("::1"), InetAddress.getByName("127.0.0.1"));
                    }
                })
                .socketFactory(sockets)
                .connectTimeout(10, TimeUnit.SECONDS)
                .callTimeout(5, TimeUnit.SECONDS)
                .build();
    }

    private static void shutdown(OkHttpClient client) {
        client.dispatcher().cancelAll();
        client.connectionPool().evictAll();
        client.dispatcher().executorService().shutdownNow();
    }

    @Test public void httpFallsBackWithoutEnablingHttpRetries() throws Exception {
        assertFalse(productionClient(HttpClientRequest.class).retryOnConnectionFailure());
        MockWebServer server = new MockWebServer();
        server.start(InetAddress.getByName("127.0.0.1"), 0);
        BlackHoleSocketFactory sockets = new BlackHoleSocketFactory();
        OkHttpClient client = blackHoleClient(HttpClientRequest.class, sockets);
        try {
            server.enqueue(new MockResponse().setBody("ipv4"));
            Request request = new Request.Builder()
                    .url(server.url("/").newBuilder().host("dual-stack.test").build())
                    .post(RequestBody.create(null, "sent once"))
                    .build();
            try (Response response = client.newCall(request).execute()) {
                assertEquals("ipv4", response.body().string());
            }
            assertTrue(sockets.ipv6Started.await(1, TimeUnit.SECONDS));
            assertTrue(sockets.ipv6Closed.await(1, TimeUnit.SECONDS));
            assertEquals(1, server.getRequestCount());
            assertEquals("sent once", server.takeRequest().getBody().readUtf8());
        } finally {
            shutdown(client);
            server.shutdown();
        }
    }

    @Test public void webSocketFallsBackBeforeUpgrade() throws Exception {
        MockWebServer server = new MockWebServer();
        server.start(InetAddress.getByName("127.0.0.1"), 0);
        BlackHoleSocketFactory sockets = new BlackHoleSocketFactory();
        OkHttpClient client = blackHoleClient(HttpClientWebSocket.class, sockets);
        WebSocket socket = null;
        try {
            server.enqueue(new MockResponse().withWebSocketUpgrade(new WebSocketListener() {}));
            CountDownLatch connected = new CountDownLatch(1);
            AtomicReference<Throwable> failure = new AtomicReference<>();
            socket = client.newWebSocket(new Request.Builder()
                    .url(server.url("/").newBuilder().host("dual-stack.test").build()).build(),
                    new WebSocketListener() {
                        @Override public void onOpen(WebSocket ws, Response response) { connected.countDown(); }
                        @Override public void onFailure(WebSocket ws, Throwable error, Response response) {
                            failure.set(error);
                            connected.countDown();
                        }
                    });
            assertTrue(connected.await(5, TimeUnit.SECONDS));
            assertNull(failure.get());
            assertTrue(sockets.ipv6Started.await(1, TimeUnit.SECONDS));
            assertTrue(sockets.ipv6Closed.await(1, TimeUnit.SECONDS));
            assertEquals(1, server.getRequestCount());
        } finally {
            if (socket != null) socket.cancel();
            shutdown(client);
            server.shutdown();
        }
    }
}
