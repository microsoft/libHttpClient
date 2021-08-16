These files are generated using the OpenSSL makefile following the build instructions here:
https://wiki.openssl.org/index.php/Compilation_and_Installation#Configuration.

The exact build commands used were:

1) Update OpenSSL to new tag
2) git clean -fxd on the OpenSSL folder
3) Install Active Perl for Windows if its not already installed
4) Open x64_x86 Cross Tools Command Prompt for VS2017
5) Run:
    perl Configure VC-WIN32 no-asm no-shared
    nmake
6) Copied the generated files to this directory.  

The libcrypto.vcxproj will automatically consume them from this directory 
so we only need to regenerate them when openssl source is updated.