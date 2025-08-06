// Example: Using HCHttpCallRequestSetMaxReceiveBufferSize for performance optimization
//
// This example shows how to use the new buffer size API to optimize performance
// for scenarios like downloading large files or when you know your responses
// will be larger than the default 16KB buffer size.

#include <httpClient/httpClient.h>
#include <iostream>

void ExampleLargeDownload()
{
    // Initialize libHttpClient
    HRESULT hr = HCInitialize(nullptr);
    if (FAILED(hr))
    {
        std::cout << "Failed to initialize libHttpClient\n";
        return;
    }

    // Create HTTP call
    HCCallHandle call;
    hr = HCHttpCallCreate(&call);
    if (FAILED(hr))
    {
        std::cout << "Failed to create HTTP call\n";
        HCCleanup();
        return;
    }

    // Set up the request
    hr = HCHttpCallRequestSetUrl(call, "GET", "https://example.com/large-file.zip");
    if (FAILED(hr))
    {
        std::cout << "Failed to set URL\n";
        HCHttpCallCloseHandle(call);
        HCCleanup();
        return;
    }

    // Set a larger receive buffer for better performance with large downloads
    // Using 64KB buffer instead of default 16KB
    const size_t largeBufferSize = 64 * 1024; // 64KB
    hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, largeBufferSize);
    if (FAILED(hr))
    {
        std::cout << "Failed to set buffer size\n";
        HCHttpCallCloseHandle(call);
        HCCleanup();
        return;
    }

    std::cout << "[PASS] Set receive buffer size to " << largeBufferSize << " bytes\n";

    // Verify the setting
    size_t retrievedBufferSize = 0;
    hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize);
    if (SUCCEEDED(hr))
    {
        std::cout << "[PASS] Confirmed buffer size: " << retrievedBufferSize << " bytes\n";
    }

    // At this point you would normally call HCHttpCallPerformAsync
    // and handle the response in your completion callback
    
    std::cout << "Ready to perform HTTP call with optimized buffer size\n";

    // Cleanup
    HCHttpCallCloseHandle(call);
    HCCleanup();
}

void ExampleMemoryConstrainedEnvironment()
{
    std::cout << "\n--- Memory Constrained Environment Example ---\n";
    
    HRESULT hr = HCInitialize(nullptr);
    if (FAILED(hr)) return;

    HCCallHandle call;
    hr = HCHttpCallCreate(&call);
    if (FAILED(hr))
    {
        HCCleanup();
        return;
    }

    hr = HCHttpCallRequestSetUrl(call, "GET", "https://api.example.com/data");
    if (FAILED(hr))
    {
        HCHttpCallCloseHandle(call);
        HCCleanup();
        return;
    }

    // Use a smaller buffer to conserve memory
    const size_t smallBufferSize = 4 * 1024; // 4KB
    hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, smallBufferSize);
    if (FAILED(hr))
    {
        std::cout << "Failed to set small buffer size\n";
        HCHttpCallCloseHandle(call);
        HCCleanup();
        return;
    }

    std::cout << "[PASS] Set smaller receive buffer size to " << smallBufferSize << " bytes for memory conservation\n";

    // Cleanup
    HCHttpCallCloseHandle(call);
    HCCleanup();
}

int main()
{
    std::cout << "LibHttpClient Buffer Size API Examples\n";
    std::cout << "======================================\n\n";

    std::cout << "--- Large Download Optimization Example ---\n";
    ExampleLargeDownload();

    ExampleMemoryConstrainedEnvironment();

    std::cout << "\n[SUCCESS] Examples completed successfully!\n";
    return 0;
}
