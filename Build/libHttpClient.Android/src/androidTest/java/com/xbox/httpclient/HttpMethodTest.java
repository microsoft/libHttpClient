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
 * Instrumentation tests for HttpClientRequest HTTP method handling.
 *
 * These tests verify that setHttpMethodAndBody() correctly sets the HTTP method
 * on the OkHttp Request.Builder for ALL HTTP verbs, not just POST/PUT.
 *
 * A regression in commit 80d9d8c moved the requestBuilder.method() call inside
 * the POST/PUT conditional, causing DELETE, HEAD, OPTIONS, and PATCH with no body
 * to never have their method set (defaulting to GET).
 *
 * Tests use two strategies:
 *   1. Reflection: Read the Request.Builder after setHttpMethodAndBody() and verify
 *      the built Request has the correct method.
 *   2. MockWebServer: Actually send the request and verify what the server received.
 */
@RunWith(AndroidJUnit4.class)
public class HttpMethodTest {

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

    // =========================================================================
    // Reflection-based tests: verify the Request.Builder method is set correctly
    // =========================================================================

    /**
     * Build an OkHttp Request from HttpClientRequest's internal requestBuilder
     * after calling setHttpUrl() and setHttpMethodAndBody().
     */
    private Request buildRequestViaReflection(String method, long contentLength) throws Exception {
        HttpClientRequest request = new HttpClientRequest(context);
        request.setHttpUrl(server.url("/test").toString());
        request.setHttpMethodAndBody(method, 0L, null, contentLength);

        Field builderField = HttpClientRequest.class.getDeclaredField("requestBuilder");
        builderField.setAccessible(true);
        Request.Builder builder = (Request.Builder) builderField.get(request);
        return builder.build();
    }

    @Test
    public void testDeleteMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("DELETE", 0);
        assertEquals("DELETE method should be set on Request", "DELETE", req.method());
    }

    @Test
    public void testGetMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("GET", 0);
        assertEquals("GET method should be set on Request", "GET", req.method());
    }

    @Test
    public void testPostMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("POST", 0);
        assertEquals("POST method should be set on Request", "POST", req.method());
    }

    @Test
    public void testPutMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("PUT", 0);
        assertEquals("PUT method should be set on Request", "PUT", req.method());
    }

    @Test
    public void testHeadMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("HEAD", 0);
        assertEquals("HEAD method should be set on Request", "HEAD", req.method());
    }

    @Test
    public void testOptionsMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("OPTIONS", 0);
        assertEquals("OPTIONS method should be set on Request", "OPTIONS", req.method());
    }

    @Test
    public void testPatchMethodSetOnRequest() throws Exception {
        Request req = buildRequestViaReflection("PATCH", 0);
        assertEquals("PATCH method should be set on Request", "PATCH", req.method());
    }

    // =========================================================================
    // MockWebServer-based tests: verify the server actually receives the right method
    // =========================================================================

    /**
     * Build the request via HttpClientRequest, then send it directly through
     * OkHttpClient to the MockWebServer and verify the recorded method.
     */
    private RecordedRequest executeAndRecordRequest(String method, long contentLength) throws Exception {
        server.enqueue(new MockResponse().setResponseCode(200).setBody("ok"));

        Request request = buildRequestViaReflection(method, contentLength);

        OkHttpClient client = new OkHttpClient.Builder()
                .connectTimeout(5, TimeUnit.SECONDS)
                .readTimeout(5, TimeUnit.SECONDS)
                .build();

        try (Response response = client.newCall(request).execute()) {
            assertNotNull("Response should not be null", response.body());
        }

        RecordedRequest recorded = server.takeRequest(5, TimeUnit.SECONDS);
        assertNotNull("Server should have received a request", recorded);
        return recorded;
    }

    @Test
    public void testDeleteMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("DELETE", 0);
        assertEquals("Server should receive DELETE", "DELETE", recorded.getMethod());
    }

    @Test
    public void testGetMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("GET", 0);
        assertEquals("Server should receive GET", "GET", recorded.getMethod());
    }

    @Test
    public void testPostMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("POST", 0);
        assertEquals("Server should receive POST", "POST", recorded.getMethod());
    }

    @Test
    public void testPutMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("PUT", 0);
        assertEquals("Server should receive PUT", "PUT", recorded.getMethod());
    }

    @Test
    public void testHeadMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("HEAD", 0);
        assertEquals("Server should receive HEAD", "HEAD", recorded.getMethod());
    }

    @Test
    public void testOptionsMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("OPTIONS", 0);
        assertEquals("Server should receive OPTIONS", "OPTIONS", recorded.getMethod());
    }

    @Test
    public void testPatchMethodReceivedByServer() throws Exception {
        RecordedRequest recorded = executeAndRecordRequest("PATCH", 0);
        assertEquals("Server should receive PATCH", "PATCH", recorded.getMethod());
    }

    // =========================================================================
    // httpbin.org E2E fallback test
    // =========================================================================

    @Test
    public void testHttpbinDeleteEcho() throws Exception {
        // Build a DELETE request using the actual HttpClientRequest class
        // and verify it works end-to-end against httpbin.org
        HttpClientRequest httpClientRequest = new HttpClientRequest(context);
        httpClientRequest.setHttpUrl("https://httpbin.org/delete");
        httpClientRequest.setHttpMethodAndBody("DELETE", 0L, null, 0L);

        Field builderField = HttpClientRequest.class.getDeclaredField("requestBuilder");
        builderField.setAccessible(true);
        Request.Builder builder = (Request.Builder) builderField.get(httpClientRequest);
        Request request = builder.build();

        assertEquals("Request method should be DELETE", "DELETE", request.method());

        OkHttpClient client = new OkHttpClient.Builder()
                .connectTimeout(10, TimeUnit.SECONDS)
                .readTimeout(10, TimeUnit.SECONDS)
                .build();

        try (Response response = client.newCall(request).execute()) {
            // httpbin.org/delete returns 200 for DELETE, 405 for GET
            assertEquals("httpbin.org/delete should return 200 for DELETE",
                    200, response.code());
        } catch (IOException e) {
            // Network may not be available in emulator — skip gracefully
            System.out.println("httpbin.org test skipped due to network: " + e.getMessage());
        }
    }
}
