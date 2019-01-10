#include "pch.h"
#include "MainPage.xaml.h"
#include <httpClient\httpClient.h>
#include <httpClient\httpProvider.h>

#include <regex>
#include <sstream>

#include <winsock2.h> 
#include "curl/curl.h"
#include <codecvt>

struct MemoryStruct
{
    char *memory;
    size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static size_t
ReadMemoryCallback(char* buffer, size_t size, size_t nitems, void* instream)
{
    size_t realsize = size * nitems;
    MemoryStruct* mem = static_cast<MemoryStruct*>(instream);

    size_t toCopy = min(realsize, mem->size);

    memcpy(buffer, mem->memory, toCopy);

    if (toCopy <= mem->size)
    {
        mem->memory += toCopy;
    }

    return toCopy;
}

std::string to_utf8string(const std::wstring &value)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.to_bytes(value);
}

std::wstring to_wstring(const std::string &value)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.from_bytes(value);
}

void STDAPIVCALLTYPE PerformCallWithCurl(
    _In_ HCCallHandle call,
    _Inout_ AsyncBlock* asyncBlock
    )
{
    const char* url = nullptr;
    const char* method = nullptr;
    const uint8_t* requestBody = nullptr;
    uint32_t requestBodySize = 0;
    const char* userAgent = nullptr;
    HCHttpCallRequestGetUrl(call, &method, &url);
    HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodySize);
    HCHttpCallRequestGetHeader(call, "User-Agent", &userAgent);
    // TODO: get/set headers for Curl to use

    CURL *curl;
    curl = curl_easy_init();
    CURLcode res;

    // from https://curl.haxx.se/libcurl/c/postinmemory.html
    struct MemoryStruct chunk = {};
    chunk.memory = (char*)malloc(1);  /* will be grown as needed by realloc above */
    chunk.size = 0;    /* no data at this point */

    struct MemoryStruct requestBodyChunk = {};
    chunk.memory = const_cast<char*>(reinterpret_cast<char const*>(requestBody));
    chunk.size = requestBodySize;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &requestBodyChunk);

    res = curl_easy_perform(curl);
    HRESULT errCode = E_FAIL;
    if (res == CURLE_OK)
    {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        HCHttpCallResponseSetStatusCode(call, response_code);
        char *ct;
        res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        HCHttpCallResponseSetResponseBodyBytes(call, (uint8_t*)chunk.memory, chunk.size);

        errCode = S_OK;
    }
    HCHttpCallResponseSetNetworkErrorCode(call, errCode, res);
    CompleteAsync(asyncBlock, errCode, 0);

    curl_easy_cleanup(curl);
}
