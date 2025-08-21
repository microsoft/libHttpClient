# Buffer Size API Tests

This directory contains tests and examples for the new LibHttpClient buffer size APIs:

- `HCHttpCallRequestSetMaxReceiveBufferSize()` 
- `HCHttpCallRequestGetMaxReceiveBufferSize()`

## Files

### BufferSizeTests.cpp
Unit tests that verify the basic functionality of the buffer size APIs:
- Setting and getting buffer sizes
- Parameter validation  
- Default value behavior
- Error handling

### Examples/BufferSizeExample.cpp
Example code demonstrating practical usage scenarios:
- Optimizing performance for large downloads (64KB buffer)
- Conserving memory in constrained environments (4KB buffer)
- Proper error handling and cleanup

## Building and Running

These test files can be compiled as standalone executables or integrated into the existing LibHttpClient test framework.

### Standalone Compilation Example:
```bash
# For tests
cl BufferSizeTests.cpp /I"..\..\..\..\Include" /link libHttpClient.lib

# For examples  
cl Examples\BufferSizeExample.cpp /I"..\..\..\..\..\Include" /link libHttpClient.lib
```

## API Usage Summary

```cpp
// Set a custom receive buffer size
size_t bufferSize = 64 * 1024; // 64KB
HRESULT hr = HCHttpCallRequestSetMaxReceiveBufferSize(call, bufferSize);

// Get the current buffer size setting
size_t currentSize;
hr = HCHttpCallRequestGetMaxReceiveBufferSize(call, &currentSize);
```

## Notes

- Buffer size must be greater than 0
- Setting buffer size to 0 returns E_INVALIDARG  
- Default buffer size is 0 (meaning use provider default, typically 16KB)
- Actual buffer size used may be limited by the underlying HTTP provider
- Must be called before `HCHttpCallPerformAsync()`
