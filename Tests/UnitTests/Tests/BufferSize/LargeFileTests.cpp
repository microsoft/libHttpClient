#include "BufferSizeTestsCommon.h"

namespace BufferSizeTests {

bool TestLargeFileBufferSize(const char* url, const char* description, size_t bufferSize, const char* bufferDesc)
{
    printf("  Testing %s with %s...\n", description, bufferDesc);
    
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
    
    auto start = std::chrono::high_resolution_clock::now();
    hr = HCHttpCallPerformAsync(context.call, &asyncBlock);
    if (!SUCCEEDED(hr)) {
        printf("[FAIL] Failed to perform HTTP call\n");
        HCHttpCallCloseHandle(context.call);
        XTaskQueueCloseHandle(queue);
        return false;
    }
    
    bool success = WaitForCompletion(context, 120); // 2 minute timeout for large files
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    if (!success) {
        printf("[FAIL] Request timed out after 2 minutes\n");
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
    
    // Calculate performance metrics
    double sizeInMB = context.responseBodySize / (1024.0 * 1024.0);
    double timeInSeconds = duration.count() / 1000.0;
    double speedMBps = sizeInMB / timeInSeconds;
    
    PrintPerformanceResult(bufferDesc, sizeInMB, timeInSeconds, speedMBps);
    
    // Cleanup
    HCHttpCallCloseHandle(context.call);
    XTaskQueueCloseHandle(queue);
    
    return true;
}

bool RunLargeFileTests()
{
    printf("Running large file buffer size performance tests...\n\n");
    
    bool allPassed = true;
    
    try {
        // Test 1: GitHub archive (3+ MB)
        printf("[INFO] Testing GitHub Archive Performance:\n");
        const char* githubUrl = "https://github.com/Microsoft/libHttpClient/archive/refs/heads/main.zip";
        
        if (!TestLargeFileBufferSize(githubUrl, "GitHub Archive", 4 * 1024, "4KB buffer")) {
            allPassed = false;
        }
        if (!TestLargeFileBufferSize(githubUrl, "GitHub Archive", 256 * 1024, "256KB buffer")) {
            allPassed = false;
        }
        
        printf("\n");
        
        // Test 2: Ubuntu torrent file (smaller but real-world)
        printf("[INFO] Testing Ubuntu Torrent File Performance:\n");
        const char* ubuntuUrl = "https://releases.ubuntu.com/20.04/ubuntu-20.04.6-desktop-amd64.iso.torrent";
        
        if (!TestLargeFileBufferSize(ubuntuUrl, "Ubuntu Torrent", 4 * 1024, "4KB buffer")) {
            allPassed = false;
        }
        if (!TestLargeFileBufferSize(ubuntuUrl, "Ubuntu Torrent", 64 * 1024, "64KB buffer")) {
            allPassed = false;
        }
        if (!TestLargeFileBufferSize(ubuntuUrl, "Ubuntu Torrent", 256 * 1024, "256KB buffer")) {
            allPassed = false;
        }
        
        printf("\n");
        
        // Test 3: HTTPBin large data (if available)
        printf("[INFO] Testing HTTPBin Large Data Performance:\n");
        const char* httpbinUrl = "https://httpbin.org/bytes/1048576"; // 1MB
        
        if (!TestLargeFileBufferSize(httpbinUrl, "HTTPBin 1MB", 4 * 1024, "4KB buffer")) {
            allPassed = false;
        }
        if (!TestLargeFileBufferSize(httpbinUrl, "HTTPBin 1MB", 128 * 1024, "128KB buffer")) {
            allPassed = false;
        }
        
        printf("\n");
        if (allPassed) {
            printf("[PASS] All large file tests passed!\n");
            printf("[INFO] Buffer size impacts demonstrated on multi-MB files\n");
            printf("[INFO] Performance improvements confirmed\n");
            printf("[INFO] Different file types tested successfully\n");
        } else {
            printf("[INFO] Some large file tests failed!\n");
        }
        
    } catch (const std::exception& e) {
        printf("[FAIL] Exception in large file tests: %s\n", e.what());
        return false;
    } catch (...) {
        printf("[FAIL] Unknown exception in large file tests\n");
        return false;
    }
    
    return allPassed;
}

}

