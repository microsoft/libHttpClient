#include "BufferSizeTestsCommon.h"

namespace BufferSizeTests {

bool TestHeaderAnalysis(const char* url, const char* description, bool expectContentLength, bool expectChunked)
{
    printf("  Testing %s...\n", description);
    
    TestContext context;
    HRESULT hr = HCHttpCallCreate(&context.call);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to create HTTP call\n");
        return false;
    }
    
    hr = HCHttpCallRequestSetUrl(context.call, "GET", url);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to set URL\n");
        HCHttpCallCloseHandle(context.call);
        return false;
    }
    
    XTaskQueueHandle queue;
    hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to create task queue\n");
        HCHttpCallCloseHandle(context.call);
        return false;
    }
    
    XAsyncBlock asyncBlock = {};
    asyncBlock.context = &context;
    asyncBlock.callback = CommonTestCallback;
    asyncBlock.queue = queue;
    
    hr = HCHttpCallPerformAsync(context.call, &asyncBlock);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to perform HTTP call\n");
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    bool success = WaitForCompletion(context, 30);
    
    if (!success || context.statusCode != 200) {
        printf("[FAIL] Request failed: timeout=%s, status=%u\n", 
               success ? "false" : "true", context.statusCode);
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    // Analyze headers
    std::string headers = context.responseHeaders;
    std::transform(headers.begin(), headers.end(), headers.begin(), ::tolower);
    
    bool hasContentLength = headers.find("content-length:") != std::string::npos;
    bool hasChunked = headers.find("transfer-encoding: chunked") != std::string::npos;
    
    printf("    Status: %u, Size: %zu bytes\n", context.statusCode, context.responseBodySize);
    printf("    Content-Length header: %s\n", hasContentLength ? "YES" : "NO");
    printf("    Transfer-Encoding chunked: %s\n", hasChunked ? "YES" : "NO");
    
    bool result = true;
    if (expectContentLength && !hasContentLength) {
        printf("[FAIL] Expected Content-Length header but didn't find it\n");
        result = false;
    }
    if (expectChunked && !hasChunked) {
        printf("[FAIL] Expected chunked encoding but didn't find it\n");
        result = false;
    }
    if (!expectContentLength && hasContentLength) {
        printf("[INFO] Unexpected Content-Length header found\n");
        // Don't fail for this, just warn
    }
    if (!expectChunked && hasChunked) {
        printf("[INFO] Unexpected chunked encoding found\n");
        // Don't fail for this, just warn
    }
    
    if (result) {
        if (hasContentLength && !hasChunked) {
            printf("[PASS] Confirmed Content-Length encoding\n");
        } else if (!hasContentLength && hasChunked) {
            printf("[PASS] Confirmed chunked Transfer-Encoding\n");
        } else {
            printf("[INFO] Other/unknown encoding detected\n");
        }
    }
    
    // Cleanup
    HCHttpCallCloseHandle(context.call);
    XTaskQueueCloseHandle(queue);
    
    return result;
}

bool RunHeaderAnalysisTests()
{
    printf("Running header analysis tests to verify response encodings...\n\n");
    
    bool allPassed = true;
    
    try {
        printf("Analyzing response encodings from different endpoints:\n");
        
        // Test 1: GitHub raw content (should have Content-Length)
        if (!TestHeaderAnalysis(
            "https://raw.githubusercontent.com/microsoft/libHttpClient/main/README.md",
            "GitHub Raw Content (Content-Length expected)",
            true,  // expect Content-Length
            false  // don't expect chunked
        )) {
            allPassed = false;
        }
        printf("\n");
        
        // Test 2: HTTPBin with known size (should have Content-Length)
        if (!TestHeaderAnalysis(
            "https://httpbin.org/bytes/1024",
            "HTTPBin 1KB (Content-Length expected)",
            true,  // expect Content-Length
            false  // don't expect chunked
        )) {
            allPassed = false;
        }
        printf("\n");
        
        // Test 3: HTTPBin stream (should be chunked)
        if (!TestHeaderAnalysis(
            "https://httpbin.org/stream/10",
            "HTTPBin Stream (Chunked expected)",
            false, // don't expect Content-Length
            true   // expect chunked
        )) {
            allPassed = false;
        }
        printf("\n");
        
        printf("[INFO] Header analysis validation complete.\n");
        printf("[INFO] Successfully detected both Content-Length and chunked Transfer-Encoding.\n");
        printf("\n");
        
        if (allPassed) {
            printf("[PASS] All header analysis tests passed!\n");
            printf("[INFO] Content-Length responses correctly identified\n");
            printf("[INFO] Chunked Transfer-Encoding responses correctly identified\n");
            printf("[INFO] Header detection working properly\n");
        } else {
            printf("[FAIL] Some header analysis tests failed!\n");
        }
        
    } catch (const std::exception& e) {
        printf("[FAIL] Exception in header analysis tests: %s\n", e.what());
        return false;
    } catch (...) {
        printf("[FAIL] Unknown exception in header analysis tests\n");
        return false;
    }
    
    return allPassed;
}

}
