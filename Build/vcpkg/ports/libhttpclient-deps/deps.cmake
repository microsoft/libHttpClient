# Third-party sources libHttpClient builds from, and where they go under the repo root.
#
# This is the single source of truth for these pins. It is read by:
#   - Build/vcpkg/Restore-Vcpkg.ps1 / restore-vcpkg.sh (via the libhttpclient-deps port in this
#     folder), which populate External/<name> for libHttpClient's own builds. They download from
#     GitHub by default, so no Microsoft-internal access is needed.
#   - The libhttpclient port in Microsoft's internal sdk.vcpkg registry, which delivers the
#     libHttpClient source tree to XAL and XSAPI with these same sources populated.
#
# Each entry: HC_DEP_<name>_REPO (GitHub owner/repo), _REF (full commit SHA, never a branch),
# _SHA512 (of https://github.com/<repo>/archive/<ref>.tar.gz), _PATH (relative to the repo root),
# _LICENSE (license file relative to _PATH; every restored source must name one).
# To bump a dependency, update its REF and SHA512 here (set SHA512 to 0 and run the restore once to
# have vcpkg report the right value). Keep this file free of quote characters.

set(HC_DEPS asio boost-wintls curl openssl websocketpp zlib)

set(HC_DEP_asio_REPO   chriskohlhoff/asio)
set(HC_DEP_asio_REF    03ae834edbace31a96157b89bf50e5ee464e5ef9) # asio-1-32-0
set(HC_DEP_asio_SHA512 6b43c85055a64077b4ffc50217384bfb77c7efd9bfd5a02058d5eea88b9c85a9a3fd6c6e71da42f60309894224819d8de52d5d5349389a0c93742c12b8020157)
set(HC_DEP_asio_PATH   External/asio)
set(HC_DEP_asio_LICENSE asio/LICENSE_1_0.txt)

set(HC_DEP_boost-wintls_REPO   laudrup/boost-wintls)
set(HC_DEP_boost-wintls_REF    5a16a857e03b8d75d0aed8a5ad918bca442dbe27)
set(HC_DEP_boost-wintls_SHA512 43df212b9e007ec7639f63fc5160ce28aa4f69f47ff35232f78ad1d36cb6fff049d6a6138ad5456155c76a7723ef613f404f6d54e8e9b4f1c4713ba8988880bf)
set(HC_DEP_boost-wintls_PATH   External/boost-wintls)
set(HC_DEP_boost-wintls_LICENSE LICENSE)

set(HC_DEP_curl_REPO   curl/curl)
set(HC_DEP_curl_REF    801bd5138ce31aa0d906fa4e2eabfc599d74e793) # curl-7_81_0
set(HC_DEP_curl_SHA512 759125bee92b8076d99468187dca78382a73471986859f9686264d9812874a2930ab38c4cf9e8314d81eb42e4b488e349d5f7819a991561553bf212527537cc9)
set(HC_DEP_curl_PATH   External/curl)
set(HC_DEP_curl_LICENSE COPYING)

set(HC_DEP_openssl_REPO   openssl/openssl)
set(HC_DEP_openssl_REF    8cf17aaeb4599f8af87fefd810b5b5fee90fe69e) # openssl-3.5.7
set(HC_DEP_openssl_SHA512 d48d434a0e2654e543e31f1595cb73b0be3d6473f09d3c211033c225697e3dcf9a439f07c28c16840c3b0f955349927c85b6cc5c4c9279b63995a40e104daad2)
set(HC_DEP_openssl_PATH   External/openssl)
set(HC_DEP_openssl_LICENSE LICENSE.txt)

set(HC_DEP_websocketpp_REPO   zaphoyd/websocketpp)
set(HC_DEP_websocketpp_REF    56123c87598f8b1dd471be83ca841ceae07f95ba) # 0.8.2
set(HC_DEP_websocketpp_SHA512 f185a66e5a7c783254352a6ef87e2e559f681032b7368765d08393ed12bcae76825abed7dcaea73de09df644320409dad46279701f5f469520542a2c9b6a6163)
set(HC_DEP_websocketpp_PATH   External/websocketpp)
set(HC_DEP_websocketpp_LICENSE COPYING)

set(HC_DEP_zlib_REPO   madler/zlib)
set(HC_DEP_zlib_REF    da607da739fa6047df13e66a2af6b8bec7c2a498) # v1.3.2
set(HC_DEP_zlib_SHA512 f04233ef9a1db985f1cd54abeb3f459889a761d8ff1632d2d0014b79699d2668e9f838ffe237d4086b352b7a89f3c48e76a65dc128c34e7fcc88fb71abbd5bd5)
set(HC_DEP_zlib_PATH   External/zlib)
set(HC_DEP_zlib_LICENSE LICENSE)
