#pragma once

#include <httpClient/httpClient.h>
#include <XAsync.h>
#include <XTaskQueue.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>

// Common test context structure
struct TestContext {
    HCCallHandle call = nullptr;
    bool completed = false;
    uint32_t statusCode = 0;
    HRESULT networkError = S_OK;
    size_t responseBodySize = 0;
    std::string responseHeaders;
    std::chrono::high_resolution_clock::time_point startTime;
};

// Common callback function
void CALLBACK CommonTestCallback(XAsyncBlock* asyncBlock);

// Common wait function
bool WaitForCompletion(TestContext& context, int timeoutSeconds = 30);

// Helper functions
void PrintTestHeader(const char* testName);
void PrintTestResult(bool passed, const char* message);
void PrintPerformanceResult(const char* description, double sizeInMB, double timeInSeconds, double speedMBps);

// Test function prototypes
namespace BufferSizeTests {
    // Unit tests
    bool RunUnitTests();
    
    // E2E tests
    bool RunE2ETests();
    
    // Header analysis tests
    bool RunHeaderAnalysisTests();
    
    // Large file tests
    bool RunLargeFileTests();
    
    // Mega file tests
    bool RunMegaFileTests();
}

// Test menu options
enum class TestType {
    All,
    Unit,
    E2E,
    Headers,
    Large,
    Mega
};

TestType ParseCommandLine(int argc, char* argv[]);
void PrintUsage(const char* programName);
