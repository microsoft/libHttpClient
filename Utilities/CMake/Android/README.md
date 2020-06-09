# libHttpClient Android CMake build system

This directory contains `CMakeLists.txt` files designed to build `libHttpClient`,
`libssl`, and `libcrypto` for Android via Gradle's "external native build"
integration with CMake.

The Gradle projects that reference these CMake builds are located in
`<project_root>/Build/XXX.Android`. The (deprecated) Visual Studio
(`.vcxproj`, `.androidproj`) builds are also located there.

To build without websockets support, define `HC_NOWEBSOCKETS` in your environment
prior to building.
