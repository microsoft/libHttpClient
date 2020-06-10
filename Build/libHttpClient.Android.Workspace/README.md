# libHttpClient.Android.Workspace

This directory defines a [Gradle composite build](https://docs.gradle.org/current/userguide/composite_builds.html)
that ties together the Gradle+CMake builds for libHttpClient, libssl, and libcrypto.

## Usage

### Build

To build, use:
```sh
[bash] $ ./gradlew assemble
```
or
```bat
[cmd] > gradlew.bat assemble
```
To build just one architecture, use the `assemble[X86|X86_64|Arm7|Arm64]` command.

### Clean

To clean, use:
```sh
[bash] $ ./gradlew clean
```
or
```bat
[cmd] > gradlew.bat clean
```
NOTE: this will remove built `.a` files from `<root>/Binaries`, but not the built `libHttpClient.aar`
(due to how Gradle clean works). To do a "hard clean", also delete the `<root>/Binaries` directory.