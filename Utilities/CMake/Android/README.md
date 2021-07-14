# libHttpClient Android CMake build system

This directory contains `CMakeLists.txt` files for building `libHttpClient`,
`libssl`, and `libcrypto` for Android via Gradle's
[external native build](https://developer.android.com/studio/projects/gradle-external-native-builds)
integration with CMake.

The Gradle projects that reference these CMake builds are located in
`/Build/<project-name>.Android`. See `/Build/libHttpClient.Android.Workspace`
for more details.
