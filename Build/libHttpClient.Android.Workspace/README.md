# libHttpClient.Android.Workspace

This directory defines a Gradle build that includes the Gradle/CMake builds for
`libHttpClient`, `libssl`, and `libcrypto` as sub-projects.

## Required tools

To use these builds, you will need Android Studio installed, with the
appropriate SDK and NDK installed. Opening this project should prompt
installation of the right required components.

Command-line builds (and CI) need:

- JDK 11 (Android Gradle Plugin 4.1 / Gradle 6.6)
- Android SDK platform 34, NDK `27.2.12479018` and CMake `3.18.1`, e.g.
  `sdkmanager --install "ndk;27.2.12479018" "cmake;3.18.1" "platforms;android-34"`
  (the SDK command-line tools themselves require JDK 17 to run).

## Outputs

This project produces binaries for the above projects for both their native
(i.e., C/C++) and Java components (for projects containing Java), as detailed
below:

- `libHttpClient`: static lib (`.a`) and Java archive (`.aar`)
- `libssl`: static lib (`.a`)
- `libcrypto`: static lib (`.a`)

Built static libs (`.a`) can be found at:
`/Binaries/Android/.cxx/<project-name>/<flavor>/<architecture>/<project-name>.a`.
The supported `<flavor>`s are `debug` and `release`, and supported
`<architecture>`s are `x86`, `x86_64`, `armeabi-v7a`, and `arm64-v8a`.

Built dynamic libs (`.so`) can be found at:
`/Binaries/Android/<project-name>/intermediates/cmake/<flavor>/obj/<architecture>/<project-name>.so`.
The supported `<flavor>`s are `debug` and `release`, and supported
`<architecture>`s are `x86`, `x86_64`, `armeabi-v7a`, and `arm64-v8a`.

Built Java products (`.aar`) can be found at:
`/Binaries/Android/<project-name>/outputs/aar/<project-name>-<flavor>.aar`. The
supported `<flavor>`s are `debug` and `release`.

## Usage

### Build

To build, open this project in Android Studio and select
`Build -> Make Project`. Note that this builds all architectures and flavors.

Alternatively, you can build it from the command-line (assuming the required
Android tools are installed and configured):

```sh
[bash] $ ./gradlew assemble
[cmd]  > gradlew.bat assemble
```

#### Building without websockets support

Passing the `HC_NOWEBSOCKETS` project property to Gradle will result in Gradle
skipping `libssl` and `libcrypto` entirely, as well as passing the
`-DHC_NOWEBSOCKETS` compiler build flag when building native code:

```sh
[bash] $ ./gradlew assemble -PHC_NOWEBSOCKETS
[cmd]  > gradlew.bat assemble -PHC_NOWEBSOCKETS
```

#### Building shared libraries

Passing the `BUILD_SHARED_LIBS` project property to Gradle will build a shared
library rather than a static library for libHttpClient:

```sh
[bash] $ ./gradlew assemble -PBUILD_SHARED_LIBS
[cmd]  > gradlew.bat assemble -PBUILD_SHARED_LIBS
```

Android Studio can also be configured to pass this flag, or it can be
configured in the project's `build.gradle`.

#### Embedding libHttpClient in another native library

A library that builds its own private copy of libHttpClient (by adding
`Build/libHttpClient.Android` to its CMake build) can set these CMake variables.
All of them are off by default and don't change the standard build:

- `HC_TARGET_NAME`: CMake target name for libHttpClient (default
  `libHttpClient.Android`), so it doesn't collide with another copy.
- `HC_EXTERNAL_XASYNC=ON`: don't compile XAsync/XTaskQueue (`Source/Task`) into
  libHttpClient. A shared build links them from `HC_XASYNC_LINK_LIBRARY`
  (required). A static build leaves them for the final link, for example when
  the embedding library defines them itself.
- `HC_ANDROID_JAVA_PACKAGE_PATH`: JNI path of the package that holds
  libHttpClient's Java classes (default `com/xbox/httpclient`). The embedding
  library must ship libHttpClient's Java classes renamed into that package.
  Native methods are then bound with `RegisterNatives` during `HCInitialize`,
  and the default-package `Java_com_xbox_httpclient_*` symbols are hidden so
  they can't bind to a public copy's Java classes.

### Clean

To clean, use:

```sh
[bash] $ ./gradlew clean
[cmd]  > gradlew.bat clean
```

Note that all intermediate and output artifacts are located in
`/Binaries/Android`, so to do a "hard clean" simply remove that directory.

### Instrumentation tests (manual)

CI only builds the Android projects. The instrumentation tests in
`Build/libHttpClient.Android/src/androidTest` (`HttpMethodTest`, `HttpHeaderTest`)
need a running emulator or attached device, so run them manually before changing
the Java/JNI HTTP or WebSocket code:

```sh
[cmd]  > gradlew.bat :libHttpClient.Android:connectedDebugAndroidTest
```

Results are written under
`/Binaries/Android/libHttpClient.Android/reports/androidTests/connected/`.
Some tests talk to a local `MockWebServer`; `HttpMethodTest.testHttpbinDeleteEcho`
needs internet access and skips itself if the network is unavailable.
