# Building libHttpClient for Linux

These scripts will be used to build the static library dependencies for libHttpClient and link them against the shared library build for libHttpClient.

## Dependencies 

You will need a Linux machine, a Linux virtual machine, or Windows Subsystem for Linux (WSL) to run the build script.

## libHttpClient_Linux.bash

Running `libHttpClient_Linux` can generate a variety of build configurations and binaries for libHttpClient and its dependencies. These binaries will be placed at `Out/x64/{Configuration}/{Library}`. Example usage is below.

```
./libHttpClient_Linux.bash
```

Running the build script with no arguments will generate a Release binary of `libcurl.a`,`libssl.a`, `libcrypto.a`, and `libHttpClient.Linux.so`.

```
./libHttpClient_Linux.bash <-c|--config> <Debug|Release>
```

Running the build script with the `-c|--config` argument will generate  Debug or Release binaries of `libssl.a`, `libcrypto.a`, `libcurl.a` and `libHttpClient.Linux.so`.

```
./libHttpClient_Linux.bash <-st|--static>
```

Running the build script with the `-st|--static` argument will generate a static lib of libHttpClient.

```
./libHttpClient_Linux.bash <-nc|--nocurl>
```

Running the build script with the `-nc|--nocurl` will **not** generate a binary of `libcurl.a`. Use this flag if you wish to bring your own version of cURL.

```
./libHttpClient_Linux.bash <-ns|--nossl>
```

Running the build script with the `-ns|--nossl` will **not** generate a binary of `libssl.a` and `libcrypto.a`. Use this flag if you wish to bring your own version of OpenSSL.

If the bash script fails to run and produces the error:
```
/bin/bash^M: bad interpreter
```
running the following command and re-running the script should fix it.
```
sed -i -e 's/\r$//' libHttpClient_Linux.bash
```

## curl_Linux.bash

libHttpClient for Linux uses cURL 7.81.0. When `curl_Linux.bash` is run, it auto generates cURL and places it in `Out/x64/{Configuration}/libcurl.Linux`.

```
./curl_Linux.bash
```

Running the build script with no arguments will generate a Release binary of `libcurl.a`.

```
./curl_Linux.bash <-c|--config> <Debug|Release>
```

Running the build script with the `-c|--config` argument will generate Debug or Release binaries of `libcurl.a`.

If you choose to use your own version of cURL, you can place your own copy of `libcurl.a` in `Out/x64/{Configuration}/libcurl.Linux`.

**libHttpClient for Linux has only been tested against version 7.81.0.**

You can build your own version of cURL from source here: https://github.com/curl/curl.

You can download a specific pre-built version of cURL here: https://curl.se/download.html.

If the bash script fails to run and produces the error:
```
/bin/bash^M: bad interpreter
```
running the following command and re-running the script should fix it.
```
sed -i -e 's/\r$//' curl_Linux.bash
```

## openssl_Linux.bash

libHttpClient for Linux uses OpenSSL 1.1.1k. When `openssl_Linux.bash` is run, it generates `libssl.a` and `libcrypto.a` and places it in `Out/x64/{Configuration}/{Library}.Linux`.

Running the build script with no arguments will generate a Release binary of `libssl.a` and `libcrypto.a`.

```
./openssl_Linux.bash <-c|--config> <Debug|Release>
```

Running the build script with the `-c|--config` argument will generate Debug or Release binaries of `libssl.a` and `libcrypto.a`.

If you choose to use your own version of OpenSSL, you can place your own copies in `Out/x64/{Configuration}/{Library}.Linux`.

**libHttpClient for Linux has only been tested against version 1.1.1k**

You can build your own version of OpenSSL from source here: https://www.openssl.org/source/.

If the bash script fails to run and produces the error:
```
/bin/bash^M: bad interpreter
```
running the following command and re-running the script should fix it.
```
sed -i -e 's/\r$//' openssl_Linux.bash
```