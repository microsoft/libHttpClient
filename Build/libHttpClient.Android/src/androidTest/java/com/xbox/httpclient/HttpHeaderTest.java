// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
package com.xbox.httpclient;

import android.content.Context;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.IOException;
import java.lang.reflect.Field;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;

import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

/**
 * Instrumentation tests for HttpClientRequest.setHttpHeader().
 *
 * Request.Builder.addHeader() throws IllegalArgumentException for non-ASCII header values
 * (GitHub issue #731). Because setHttpHeader() is called from native code, that exception
 * aborted the process. Headers are now added leniently and sent as UTF-8.
 */
@RunWith(AndroidJUnit4.class)
public class HttpHeaderTest {

    private static final String NON_ASCII_VALUE = "\u4e2d\u56fd\u7535\u4fe1"; // 中国电信

    private Context context;
    private MockWebServer server;

    @Before
    public void setUp() throws IOException {
        context = InstrumentationRegistry.getInstrumentation().getTargetContext();
        server = new MockWebServer();
        server.start();
    }

    @After
    public void tearDown() throws IOException {
        server.shutdown();
    }

    private HttpClientRequest newGetRequest() {
        HttpClientRequest request = new HttpClientRequest(context);
        request.setHttpUrl(server.url("/headers").toString());
        request.setHttpMethodAndBody("GET", 0L, null, 0L);
        return request;
    }

    private static Request buildRequest(HttpClientRequest request) throws Exception {
        Field builderField = HttpClientRequest.class.getDeclaredField("requestBuilder");
        builderField.setAccessible(true);
        return ((Request.Builder) builderField.get(request)).build();
    }

    private RecordedRequest send(Request request) throws Exception {
        server.enqueue(new MockResponse().setResponseCode(200).setBody("ok"));

        OkHttpClient client = new OkHttpClient.Builder()
                .connectTimeout(5, TimeUnit.SECONDS)
                .readTimeout(5, TimeUnit.SECONDS)
                .build();

        try (Response response = client.newCall(request).execute()) {
            assertEquals(200, response.code());
        }

        RecordedRequest recorded = server.takeRequest(5, TimeUnit.SECONDS);
        assertNotNull("Server should have received a request", recorded);
        return recorded;
    }

    @Test
    public void testNonAsciiHeaderValueDoesNotThrow() throws Exception {
        HttpClientRequest request = newGetRequest();
        request.setHttpHeader("carrier", NON_ASCII_VALUE);

        assertEquals(NON_ASCII_VALUE, buildRequest(request).header("carrier"));
    }

    @Test
    public void testNonAsciiHeaderValueSentAsUtf8() throws Exception {
        HttpClientRequest request = newGetRequest();
        request.setHttpHeader("carrier", NON_ASCII_VALUE);

        RecordedRequest recorded = send(buildRequest(request));
        assertEquals(NON_ASCII_VALUE, recorded.getHeader("carrier"));
    }

    @Test
    public void testAsciiHeadersStillSent() throws Exception {
        HttpClientRequest request = newGetRequest();
        request.setHttpHeader("Authorization", "XBL3.0 x=1;token");
        request.setHttpHeader("x-xbl-contract-version", "2");

        RecordedRequest recorded = send(buildRequest(request));
        assertEquals("XBL3.0 x=1;token", recorded.getHeader("Authorization"));
        assertEquals("2", recorded.getHeader("x-xbl-contract-version"));
    }

    @Test
    public void testRepeatedHeaderNamesArePreserved() throws Exception {
        HttpClientRequest request = newGetRequest();
        request.setHttpHeader("x-multi", "a");
        request.setHttpHeader("x-multi", "b");

        assertEquals(Arrays.asList("a", "b"), buildRequest(request).headers("x-multi"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testInvalidHeaderNameStillThrows() throws Exception {
        // Names must remain valid tokens; native AddHeader() turns this into a failed call.
        newGetRequest().setHttpHeader("bad name\u4e2d", "value");
    }
}
