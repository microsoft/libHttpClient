// Test for HCHttpCallRequestSetMaxReceiveBufferSize/GetMaxReceiveBufferSize
#include <httpClient/httpClient.h>
#include <cassert>
#include <cstdio>

int main()
{
    // Initialize libHttpClient
    HRESULT hr = HCInitialize(nullptr);
    assert(SUCCEEDED(hr));

    // Create HTTP call
    HCCallHandle call;
    hr = HCHttpCallCreate(&call);
    assert(SUCCEEDED(hr));

    // Test setting and getting buffer size
    const size_t testBufferSize = 64 * 1024; // 64KB
    
    // Set buffer size
    hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, testBufferSize);
    assert(SUCCEEDED(hr));
    printf("✓ Successfully set buffer size to %zu bytes\n", testBufferSize);

    // Get buffer size
    size_t retrievedBufferSize = 0;
    hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize);
    assert(SUCCEEDED(hr));
    assert(retrievedBufferSize == testBufferSize);
    printf("✓ Successfully retrieved buffer size: %zu bytes\n", retrievedBufferSize);

    // Test invalid parameters
    hr = HCHttpCallRequestSetMaxReceiveBufferSize(nullptr, testBufferSize);
    assert(hr == E_INVALIDARG);
    printf("✓ Correctly rejected null call handle\n");

    hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, 0);
    assert(hr == E_INVALIDARG);
    printf("✓ Correctly rejected zero buffer size\n");

    hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, nullptr);
    assert(hr == E_INVALIDARG);
    printf("✓ Correctly rejected null output parameter\n");

    // Test default value (should be 0)
    HCCallHandle call2;
    hr = HCHttpCallCreate(&call2);
    assert(SUCCEEDED(hr));

    size_t defaultBufferSize = 999; // Initialize to non-zero
    hr = HCHttpCallRequestGetMaxReceiveBufferSize(call2, &defaultBufferSize);
    assert(SUCCEEDED(hr));
    assert(defaultBufferSize == 0);
    printf("✓ Default buffer size is 0 (use provider default)\n");

    // Cleanup
    HCHttpCallCloseHandle(call);
    HCHttpCallCloseHandle(call2);
    HCCleanup();

    printf("\n🎉 All tests passed!\n");
    return 0;
}
