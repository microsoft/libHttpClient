# libHttpClient.Android.Workspace

This directory defines a Gradle build that includes the
Gradle+CMake builds for libHttpClient, libssl, and libcrypto
as sub-projects.

## Usage

### Build

To build, use:
```
[cmd] > gradlew.bat assemble
[bash] $ ./gradlew assemble
```
NOTE:
- To build just one architecture, use the `assemble[X86|X86_64|Arm7|Arm64]`
command.
- To build just one config, use the `assemble[Debug|Release]` command.
- To build just one arch+config combination, use the
`assemble[X86|X86_64|Arm7|Arm64][Debug|Release]` command, e.g. `assembleArm7Debug`.

#### Building without websockets support

Passing the `-PHC_NOWEBSOCKETS` argument to Gradle (`-P` referring to "project
properties") will result in Gradle skipping `libssl` and `libcrypto` entirely,
as well as passing the `-DHC_NOWEBSOCKETS` compiler build flag when building
`libHttpClient`.

E.g.:
```
[cmd] > gradlew.bat assemble -PHC_NOWEBSOCKETS
[bash] $ ./gradlew assemble -PHC_NOWEBSOCKETS
```

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
