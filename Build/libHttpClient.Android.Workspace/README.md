# libHttpClient.Android.Workspace

This directory defines a Gradle build that includes the
Gradle+CMake builds for libHttpClient, libssl, and libcrypto
as sub-projects.

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
NOTE:
- To build just one architecture, use the `assemble[X86|X86_64|Arm7|Arm64]`
command.
- To build just one config, use the `assemble[Debug|Release]` command.
- To build just one arch+config combination, use the
`assemble[X86|X86_64|Arm7|Arm64][Debug|Release]` command, e.g. `assembleArm7Debug`.

### Clean

To clean, use:
```sh
[bash] $ ./gradlew clean
```
or
```bat
[cmd] > gradlew.bat clean
```
NOTE:
- This removes the build directory (typically `app/build`) for each dependent
build, as well as deletes the built `.a` files from `<root>/Binaries`. It will
not delete the built `libHttpClient.aar`, due to how Gradle clean works. To do
a "hard clean", also delete the `<root>/Binaries` directory.
