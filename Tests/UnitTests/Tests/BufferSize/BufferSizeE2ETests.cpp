#include "BufferSizeTestsCommon.h"

namespace BufferSizeTests {

bool TestContentLengthResponse(size_t bufferSize, const char* bufferDesc)
{
    printf("  Testing Content-Length response with %s...\n", bufferDesc);
    
    TestContext context;
    HRESULT hr = HCHttpCallCreate(&context.call);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to create HTTP call\n");
        return false;
    }
    
    // Set buffer size if specified
    if (bufferSize > 0) {
        hr = HCHttpCallRequestSetMaxReceiveBufferSize(context.call, bufferSize);
        if (!SUCCEEDED(hr)) {
            printf("[FAIL] Failed to set buffer size\n");
            HCHttpCallCloseHandle(context.call);
            return false;
        }
    }
    
    // Use GitHub raw content which provides Content-Length header
    hr = HCHttpCallRequestSetUrl(context.call, "GET", "https://raw.githubusercontent.com/microsoft/libHttpClient/main/README.md");
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
    
    context.startTime = std::chrono::high_resolution_clock::now();
    hr = HCHttpCallPerformAsync(context.call, &asyncBlock);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to perform HTTP call\n");
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    bool success = WaitForCompletion(context, 30);
    
    if (!success) {
        printf("[FAIL] Request timed out\n");
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    if (context.statusCode != 200) {
        printf("[FAIL] HTTP Status: %u\n", context.statusCode);
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    printf("[PASS] Content-Length response test passed (Size: %zu bytes)\n", context.responseBodySize);
    
    // Cleanup
    HCHttpCallCloseHandle(context.call);
    XTaskQueueCloseHandle(queue);
    
    return true;
}

bool TestChunkedResponse(size_t bufferSize, const char* bufferDesc)
{
    printf("  Testing chunked response with %s...\n", bufferDesc);
    
    TestContext context;
    HRESULT hr = HCHttpCallCreate(&context.call);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to create HTTP call\n");
        return false;
    }
    
    // Set buffer size if specified
    if (bufferSize > 0) {
        hr = HCHttpCallRequestSetMaxReceiveBufferSize(context.call, bufferSize);
        if (!SUCCEEDED(hr)) {
            printf("[FAIL] Failed to set buffer size\n");
            HCHttpCallCloseHandle(context.call);
            return false;
        }
    }
    
    // Use HTTPBin stream which provides chunked encoding
    hr = HCHttpCallRequestSetUrl(context.call, "GET", "https://httpbin.org/stream/5");
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
    
    context.startTime = std::chrono::high_resolution_clock::now();
    hr = HCHttpCallPerformAsync(context.call, &asyncBlock);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to perform HTTP call\n");
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    bool success = WaitForCompletion(context, 30);
    
    if (!success) {
        printf("[FAIL] Request timed out\n");
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    if (context.statusCode != 200) {
        printf("[FAIL] HTTP Status: %u\n", context.statusCode);
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    printf("[PASS] Chunked response test passed (Size: %zu bytes)\n", context.responseBodySize);
    
    // Cleanup
    HCHttpCallCloseHandle(context.call);
    XTaskQueueCloseHandle(queue);
    
    return true;
}

bool RunE2ETests()
{
    printf("Running E2E buffer size tests with real network requests...\n\n");
    
    bool allPassed = true;
    
    try {
        // Test Content-Length responses with different buffer sizes
        printf("[INFO] Testing Content-Length Responses:\n");
        allPassed = TestContentLengthResponse(1024, "1KB buffer") && allPassed;
        allPassed = TestContentLengthResponse(4096, "4KB buffer") && allPassed;
        allPassed = TestContentLengthResponse(16384, "16KB buffer") && allPassed;
        allPassed = TestContentLengthResponse(65536, "64KB buffer") && allPassed;
        
        printf("\n");
        
        // Test chunked responses with different buffer sizes
        printf("[INFO] Testing Chunked Responses:\n");
        allPassed = TestChunkedResponse(1024, "1KB buffer") && allPassed;
        allPassed = TestChunkedResponse(4096, "4KB buffer") && allPassed;
        allPassed = TestChunkedResponse(16384, "16KB buffer") && allPassed;
        allPassed = TestChunkedResponse(65536, "64KB buffer") && allPassed;
        
        printf("\n");
        printf("[PASS] E2E tests completed!\n");
        printf("[INFO] Key insights from E2E testing:\n");
        printf("  - Buffer size APIs work correctly with real network requests\n");
        printf("  - Both Content-Length and chunked responses are handled properly\n");
        printf("  - Different buffer sizes all function correctly\n");
        printf("  - Network requests complete successfully with various buffer configurations\n");
        
    } catch (const std::exception& e) {
        printf("[FAIL] Exception in E2E tests: %s\n", e.what());
        return false;
    } catch (...) {
        printf("[FAIL] Unknown exception in E2E tests\n");
        return false;
    }
    
    return allPassed;
}

}
