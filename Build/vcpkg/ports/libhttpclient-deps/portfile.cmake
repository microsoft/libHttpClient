# libhttpclient-deps: downloads the third-party sources listed in deps.cmake (pinned commit +
# SHA512) and installs them, unbuilt, under share/libhttpclient-deps/src/<path>.
# Restore-Vcpkg.ps1 / restore-vcpkg.sh then copy them into External/<name>.
#
# deps.cmake lives in this folder on purpose: vcpkg keys its package cache on the files of the
# port, so any pin change automatically invalidates previously restored sources.

set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_ALLOW_EMPTY_FOLDERS enabled)
set(VCPKG_POLICY_SKIP_ALL_POST_BUILD_CHECKS enabled)

include("${CMAKE_CURRENT_LIST_DIR}/deps.cmake")

set(_src "${CURRENT_PACKAGES_DIR}/share/${PORT}/src")
foreach(_dep IN LISTS HC_DEPS)
    vcpkg_from_github(
        OUT_SOURCE_PATH _dep_source
        REPO "${HC_DEP_${_dep}_REPO}"
        REF "${HC_DEP_${_dep}_REF}"
        SHA512 "${HC_DEP_${_dep}_SHA512}"
    )
    file(COPY "${_dep_source}/." DESTINATION "${_src}/${HC_DEP_${_dep}_PATH}")
endforeach()

file(WRITE "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright"
    "Third-party sources used by libHttpClient. Each is covered by its own license, included in its source tree.\n")
