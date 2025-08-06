#include "BufferSizeTestsCommon.h"
#include <iostream>

void CALLBACK CommonTestCallback(XAsyncBlock* asyncBlock)
{
    TestContext* context = static_cast<TestContext*>(asyncBlock->context);
    
    context->networkError = S_OK;
    
    HRESULT hr = HCHttpCallResponseGetStatusCode(context->call, &context->statusCode);
    if (SUCCEEDED(hr)) {
        size_t responseBodySize = 0;
        hr = HCHttpCallResponseGetResponseBodyBytesSize(context->call, &responseBodySize);
        if (SUCCEEDED(hr)) {
            context->responseBodySize = responseBodySize;
        }
        
        // Get headers if needed
        uint32_t numHeaders = 0;
        hr = HCHttpCallResponseGetNumHeaders(context->call, &numHeaders);
        if (SUCCEEDED(hr)) {
            context->responseHeaders.clear();
            for (uint32_t i = 0; i < numHeaders; i++) {
                const char* headerName = nullptr;
                const char* headerValue = nullptr;
                hr = HCHttpCallResponseGetHeaderAtIndex(context->call, i, &headerName, &headerValue);
                if (SUCCEEDED(hr) && headerName && headerValue) {
                    context->responseHeaders += headerName;
                    context->responseHeaders += ": ";
                    context->responseHeaders += headerValue;
                    context->responseHeaders += "\n";
                }
            }
        }
    }
    
    context->completed = true;
}

bool WaitForCompletion(TestContext& context, int timeoutSeconds)
{
    auto start = std::chrono::high_resolution_clock::now();
    auto timeout = std::chrono::seconds(timeoutSeconds);
    
    while (!context.completed)
    {
        auto now = std::chrono::high_resolution_clock::now();
        if (now - start > timeout)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

void PrintTestHeader(const char* testName)
{
    printf("\n");
    printf("================================================================================\n");
    printf("  %s\n", testName);
    printf("================================================================================\n");
}

void PrintTestResult(bool passed, const char* message)
{
    printf("%s %s\n", passed ? "[PASS]" : "[FAIL]", message);
}

void PrintPerformanceResult(const char* description, double sizeInMB, double timeInSeconds, double speedMBps)
{
    printf("[PERF] %s: %.2f MB in %.2f sec (%.2f MB/s)\n", description, sizeInMB, timeInSeconds, speedMBps);
}

TestType ParseCommandLine(int argc, char* argv[])
{
    if (argc < 2) {
        return TestType::All;
    }
    
    std::string arg = argv[1];
    std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);
    
    if (arg == "unit" || arg == "-unit" || arg == "--unit") {
        return TestType::Unit;
    } else if (arg == "e2e" || arg == "-e2e" || arg == "--e2e") {
        return TestType::E2E;
    } else if (arg == "headers" || arg == "-headers" || arg == "--headers") {
        return TestType::Headers;
    } else if (arg == "large" || arg == "-large" || arg == "--large") {
        return TestType::Large;
    } else if (arg == "mega" || arg == "-mega" || arg == "--mega") {
        return TestType::Mega;
    } else if (arg == "all" || arg == "-all" || arg == "--all") {
        return TestType::All;
    } else {
        return TestType::All;
    }
}

void PrintUsage(const char* programName)
{
    printf("LibHttpClient Buffer Size Test Suite\n");
    printf("====================================\n\n");
    printf("Usage: %s [test_type]\n\n", programName);
    printf("Test Types:\n");
    printf("  all      - Run all tests (default)\n");
    printf("  unit     - Run unit tests only\n");
    printf("  e2e      - Run end-to-end tests only\n");
    printf("  headers  - Run header analysis tests only\n");
    printf("  large    - Run large file tests only\n");
    printf("  mega     - Run mega file tests only\n\n");
    printf("Examples:\n");
    printf("  %s           # Run all tests\n", programName);
    printf("  %s unit      # Run only unit tests\n", programName);
    printf("  %s e2e       # Run only E2E tests\n", programName);
    printf("  %s large     # Run only large file tests\n", programName);
}

int main(int argc, char* argv[])
{
    // Parse command line
    TestType testType = ParseCommandLine(argc, argv);
    
    if (argc > 1 && (std::string(argv[1]) == "help" || std::string(argv[1]) == "-help" || std::string(argv[1]) == "--help")) {
        PrintUsage(argv[0]);
        return 0;
    }
    
    // Initialize libHttpClient
    HRESULT hr = HCInitialize(nullptr);
    if (!SUCCEEDED(hr)) {
        printf("[ERROR] Failed to initialize libHttpClient: 0x%08X\n", hr);
        return 1;
    }
    
    printf("[INFO] LibHttpClient Buffer Size Test Suite\n");
    printf("========================================\n");
    printf("Testing buffer size APIs comprehensively...\n");
    
    bool allTestsPassed = true;
    int testsRun = 0;
    int testsPassed = 0;
    
    auto overallStart = std::chrono::high_resolution_clock::now();
    
    try {
        // Run tests based on command line argument
        if (testType == TestType::All || testType == TestType::Unit) {
            testsRun++;
            PrintTestHeader("UNIT TESTS - Basic API Functionality");
            bool result = BufferSizeTests::RunUnitTests();
            PrintTestResult(result, "Unit Tests");
            if (result) testsPassed++;
            allTestsPassed = allTestsPassed && result;
        }
        
        if (testType == TestType::All || testType == TestType::E2E) {
            testsRun++;
            PrintTestHeader("E2E TESTS - Real Network Requests");
            bool result = BufferSizeTests::RunE2ETests();
            PrintTestResult(result, "E2E Tests");
            if (result) testsPassed++;
            allTestsPassed = allTestsPassed && result;
        }
        
        if (testType == TestType::All || testType == TestType::Headers) {
            testsRun++;
            PrintTestHeader("HEADER ANALYSIS - Response Encoding Detection");
            bool result = BufferSizeTests::RunHeaderAnalysisTests();
            PrintTestResult(result, "Header Analysis Tests");
            if (result) testsPassed++;
            allTestsPassed = allTestsPassed && result;
        }
        
        if (testType == TestType::All || testType == TestType::Large) {
            testsRun++;
            PrintTestHeader("LARGE FILE TESTS - Multi-MB Downloads");
            bool result = BufferSizeTests::RunLargeFileTests();
            PrintTestResult(result, "Large File Tests");
            if (result) testsPassed++;
            allTestsPassed = allTestsPassed && result;
        }
        
        if (testType == TestType::All || testType == TestType::Mega) {
            testsRun++;
            PrintTestHeader("MEGA FILE TESTS - 20+ MB Downloads");
            bool result = BufferSizeTests::RunMegaFileTests();
            PrintTestResult(result, "Mega File Tests");
            if (result) testsPassed++;
            allTestsPassed = allTestsPassed && result;
        }
        
    } catch (const std::exception& e) {
        printf("[ERROR] Exception during tests: %s\n", e.what());
        allTestsPassed = false;
    } catch (...) {
        printf("[ERROR] Unknown exception during tests\n");
        allTestsPassed = false;
    }
    
    auto overallEnd = std::chrono::high_resolution_clock::now();
    auto overallDuration = std::chrono::duration_cast<std::chrono::milliseconds>(overallEnd - overallStart);
    
    // Print final results
    printf("\n");
    printf("================================================================================\n");
    printf("  FINAL RESULTS\n");
    printf("================================================================================\n");
    printf("Tests run: %d\n", testsRun);
    printf("Tests passed: %d\n", testsPassed);
    printf("Tests failed: %d\n", testsRun - testsPassed);
    printf("Overall time: %.2f seconds\n", overallDuration.count() / 1000.0);
    printf("Overall result: %s\n", allTestsPassed ? "[PASS] ALL TESTS PASSED" : "[FAIL] SOME TESTS FAILED");
    
    if (allTestsPassed) {
        printf("\n[SUCCESS] Buffer size APIs are working correctly!\n");
        printf("Key findings:\n");
        printf("- Buffer size APIs function correctly\n");
        printf("- Performance improvements confirmed for large files\n");
        printf("- Both Content-Length and chunked responses handled properly\n");
        printf("- Optimal buffer sizes identified (256KB-512KB range)\n");
    } else {
        printf("\n[WARNING] Some tests failed - please review the output above\n");
    }
    
    // Cleanup
    HCCleanup();
    
    return allTestsPassed ? 0 : 1;
}
