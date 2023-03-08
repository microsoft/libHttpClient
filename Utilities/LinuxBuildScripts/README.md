# What are these scripts for?

The purpose for these scripts are to build the required static libraries for different LibHttpClient platforms. You can use your own version of the static library if you choose to.

If you choose to use your own version of cURL, you can place your own copy of libcurl.a in Binaries/Release/x64/libcurl.Linux. LibHttpClient for Linux has only been tested against version 7.81.0.

You can build your own version of cURL from source here: https://github.com/curl/curl.

You can download a specific pre-built version of cURL here:https://curl.se/download.html.

## Linux

LibHttpClient for Linux uses cURL 7.81.0. When you run ```curl.bash``` it will create a static lib and be placed in Binaries/Release/x64/libcurl.Linux.

You will need a Linux machine, a Linux vurtual machine, or Windows Subsystem for Linux (WSL) to run the bash script.

If the bash script fails to run and produces the error ```/bin/bash^M: bad interpreter``` use ```sed -i -e 's/\r$//' curl.bash``` then run the script again.