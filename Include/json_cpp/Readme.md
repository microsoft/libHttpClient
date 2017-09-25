## Description

This is a standalone C++ JSON parser using slightly modified source from https://github.com/Microsoft/cpprestsdk
</p>
This is provided for C++ games that need a C++ JSON parser but don't want to take a full dependancy on the cpprestsdk.

## Usage

Just #include <json_cpp/json.h> in your C++ project then you can use cpprestsdk JSON parser as normal.  No need to add any source to your project.  For example:

    #include <json_cpp/json.h>
...

    std::error_code err;
    std::wstring exampleString = L"{\"exampleName\":\"exampleValue\"}";
    web::json::value jsonConfig = web::json::value::parse(exampleString, err);
    
## How to its made

The modifications from the original https://github.com/Microsoft/cpprestsdk can be seen at https://github.com/jasonsandlin/cpprestsdk/tree/json_standalone.  
</p>
You can regenerate the layout of these standalone files using MakeJsonStandalone.cmd in that branch.


