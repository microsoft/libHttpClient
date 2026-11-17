cmake_minimum_required(VERSION 3.14)
project(HappyEyeballsTests LANGUAGES CXX)
enable_testing()
get_filename_component(HC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${HC_ROOT}/Build/vcpkg/AutoRestore.cmake")
add_executable(HappyEyeballsTests HappyEyeballsTests.cpp)
target_compile_features(HappyEyeballsTests PRIVATE cxx_std_17)
target_compile_definitions(HappyEyeballsTests PRIVATE ASIO_STANDALONE)
target_include_directories(HappyEyeballsTests PRIVATE
    "${HC_ROOT}/External/asio/asio/include"
    "${HC_ROOT}/Source/WebSocket/Websocketpp")
if(WIN32)
    target_link_libraries(HappyEyeballsTests PRIVATE ws2_32 mswsock)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(HappyEyeballsTests PRIVATE Threads::Threads)
endif()
add_test(NAME HappyEyeballsTests COMMAND HappyEyeballsTests)
set_tests_properties(HappyEyeballsTests PROPERTIES TIMEOUT 30 LABELS "unit")

# Compile-only coverage of the OpenSSL socket policy, without linking OpenSSL.
add_library(HappyEyeballsTlsCompile OBJECT TlsEndpointCompile.cpp)
target_compile_features(HappyEyeballsTlsCompile PRIVATE cxx_std_17)
target_compile_definitions(HappyEyeballsTlsCompile PRIVATE
    ASIO_STANDALONE _WEBSOCKETPP_CPP11_STL_ _WEBSOCKETPP_CPP11_RANDOM_DEVICE_)
if(WIN32)
    set(HC_TLS_GENERATED_HEADERS win32)
else()
    set(HC_TLS_GENERATED_HEADERS linux)
endif()
target_include_directories(HappyEyeballsTlsCompile PRIVATE
    "${HC_ROOT}/External/asio/asio/include"
    "${HC_ROOT}/External/websocketpp"
    "${HC_ROOT}/External/openssl/include"
    "${HC_ROOT}/External/opensslGeneratedHeaders/${HC_TLS_GENERATED_HEADERS}"
    "${HC_ROOT}/Source/WebSocket/Websocketpp")
