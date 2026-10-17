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
import java.nio.charset.StandardCharsets;
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
import static org.junit.Assert.fail;

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

    @Test
    public void testControlCharactersInHeaderValueRejected() throws Exception {
        // addUnsafeNonAscii doesn't validate values, so CR/LF would otherwise inject extra headers.
        String[] injected = { "foo\r\nX-Injected: 1", "foo\nX-Injected: 1", "foo\rbar", "foo\u0000bar", "foo\u007fbar", "foo\u001bbar" };
        for (String value : injected) {
            try {
                newGetRequest().setHttpHeader("x-test", value);
                fail("Expected IllegalArgumentException for a control character in: " + value.replace("\r", "\\r").replace("\n", "\\n"));
            } catch (IllegalArgumentException expected) {
                // The native caller maps this to E_INVALIDARG.
            }
        }
    }

    @Test
    public void testTabInHeaderValueAllowed() throws Exception {
        HttpClientRequest request = newGetRequest();
        request.setHttpHeader("x-tab", "a\tb");

        assertEquals("a\tb", buildRequest(request).header("x-tab"));
    }

    @Test
    public void testUtf8BytesWithSupplementaryCharacterSent() throws Exception {
        // Native code passes raw UTF-8 bytes; a 4-byte character (emoji) isn't valid Modified UTF-8.
        String value = "player \ud83c\udfae \u4e2d";
        HttpClientRequest request = newGetRequest();
        request.setHttpHeader("x-name".getBytes(StandardCharsets.UTF_8), value.getBytes(StandardCharsets.UTF_8));

        RecordedRequest recorded = send(buildRequest(request));
        assertEquals(value, recorded.getHeader("x-name"));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testUtf8BytesWithCrLfRejected() throws Exception {
        newGetRequest().setHttpHeader("x-test".getBytes(StandardCharsets.UTF_8), "foo\r\nX-Injected: 1".getBytes(StandardCharsets.UTF_8));
    }
}
