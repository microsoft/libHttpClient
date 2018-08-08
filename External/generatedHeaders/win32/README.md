These files are generated using the OpenSSL makefile following the build instructions here https://wiki.openssl.org/index.php/Compilation_and_Installation#Configuration.

The exact build commands used were:

perl Configure VC-WIN32 no-asm no-shared
nmake

After this I copied the generated files to this directory.  The libcrypto.vcxproj will automatically consume them from this directory so we only need to regenerate them when openssl source is updated.