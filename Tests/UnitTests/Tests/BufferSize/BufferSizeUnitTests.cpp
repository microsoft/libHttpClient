#include "BufferSizeTestsCommon.h"

namespace BufferSizeTests {

bool RunUnitTests()
{
    printf("Running buffer size API unit tests...\n\n");
    
    bool allPassed = true;
    
    try {
        // Create HTTP call
        HCCallHandle call;
        HRESULT hr = HCHttpCallCreate(&call);
        if (!SUCCEEDED(hr)) {
            printf("[ERROR] Failed to create HTTP call: 0x%08X\n", hr);
            return false;
        }

        // Test setting and getting buffer size
        const size_t testBufferSize = 64 * 1024; // 64KB
        
        // Set buffer size
        hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, testBufferSize);
        if (SUCCEEDED(hr)) {
            printf("[PASS] Successfully set buffer size to %zu bytes\n", testBufferSize);
        } else {
            printf("[ERROR] Failed to set buffer size: 0x%08X\n", hr);
            allPassed = false;
        }

        // Get buffer size
        size_t retrievedBufferSize = 0;
        hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize);
        if (SUCCEEDED(hr) && retrievedBufferSize == testBufferSize) {
            printf("[PASS] Successfully retrieved buffer size: %zu bytes\n", retrievedBufferSize);
        } else {
            printf("[ERROR] Failed to retrieve correct buffer size. Expected: %zu, Got: %zu, HR: 0x%08X\n", 
                   testBufferSize, retrievedBufferSize, hr);
            allPassed = false;
        }

        // Test invalid parameters
        hr = HCHttpCallRequestSetMaxReceiveBufferSize(nullptr, testBufferSize);
        if (hr == E_INVALIDARG) {
            printf("[PASS] Correctly rejected null call handle\n");
        } else {
            printf("[ERROR] Should have rejected null call handle, got: 0x%08X\n", hr);
            allPassed = false;
        }

        // Test setting zero buffer size (reset to provider default)
        hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, 0);
        if (SUCCEEDED(hr)) {
            printf("[PASS] Successfully set buffer size to 0 (provider default)\n");
        } else {
            printf("[ERROR] Failed to set buffer size to 0: 0x%08X\n", hr);
            allPassed = false;
        }

        hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize);
        if (SUCCEEDED(hr) && retrievedBufferSize == 0) {
            printf("[PASS] Confirmed buffer size reset to 0 (provider default)\n");
        } else {
            printf("[ERROR] Buffer size not reset to 0. Got: %zu, HR: 0x%08X\n", retrievedBufferSize, hr);
            allPassed = false;
        }

        hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, nullptr);
        if (hr == E_INVALIDARG) {
            printf("[PASS] Correctly rejected null output parameter\n");
        } else {
            printf("[ERROR] Should have rejected null output parameter, got: 0x%08X\n", hr);
            allPassed = false;
        }

        // Test default value (should be 0)
        HCCallHandle call2;
        hr = HCHttpCallCreate(&call2);
        if (!SUCCEEDED(hr)) {
            printf("[ERROR] Failed to create second HTTP call: 0x%08X\n", hr);
            HCHttpCallCloseHandle(call);
            return false;
        }

        size_t defaultBufferSize = 999; // Initialize to non-zero
        hr = HCHttpCallRequestGetMaxReceiveBufferSize(call2, &defaultBufferSize);
        if (SUCCEEDED(hr) && defaultBufferSize == 0) {
            printf("[PASS] Default buffer size is 0 (use provider default)\n");
        } else {
            printf("[ERROR] Default buffer size should be 0. Got: %zu, HR: 0x%08X\n", defaultBufferSize, hr);
            allPassed = false;
        }

        // Test various buffer sizes
        printf("\n--- Testing various buffer sizes ---\n");
        size_t testSizes[] = {
            1024,           // 1KB
            4 * 1024,       // 4KB
            16 * 1024,      // 16KB
            64 * 1024,      // 64KB
            256 * 1024,     // 256KB
            1024 * 1024,    // 1MB
            4 * 1024 * 1024 // 4MB
        };
        
        for (size_t testSize : testSizes) {
            hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, testSize);
            if (SUCCEEDED(hr)) {
                size_t retrieved = 0;
                hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrieved);
                if (SUCCEEDED(hr) && retrieved == testSize) {
                    printf("[PASS] Buffer size %zu bytes set and retrieved correctly\n", testSize);
                } else {
                    printf("[ERROR] Buffer size %zu bytes: set OK but retrieve failed (got %zu)\n", testSize, retrieved);
                    allPassed = false;
                }
            } else {
                printf("[ERROR] Failed to set buffer size %zu bytes: 0x%08X\n", testSize, hr);
                allPassed = false;
            }
        }

        // Cleanup
        HCHttpCallCloseHandle(call);
        HCHttpCallCloseHandle(call2);

        printf("\n");
        if (allPassed) {
            printf("[PASS] All unit tests passed!\n");
        } else {
            printf("[ERROR] Some unit tests failed!\n");
        }

    } catch (const std::exception& e) {
        printf("[ERROR] Exception in unit tests: %s\n", e.what());
        return false;
    } catch (...) {
        printf("[ERROR] Unknown exception in unit tests\n");
        return false;
    }
    
    return allPassed;
}

}

