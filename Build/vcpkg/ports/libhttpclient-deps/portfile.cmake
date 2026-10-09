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

# Copyright: each restored source's own license file (HC_DEP_<name>_LICENSE in deps.cmake).
set(_copyright "${CURRENT_PACKAGES_DIR}/share/${PORT}/copyright")
file(WRITE "${_copyright}" "Third-party sources used by libHttpClient, each under its own license:\n")
foreach(_dep IN LISTS HC_DEPS)
    if(NOT DEFINED HC_DEP_${_dep}_LICENSE)
        message(FATAL_ERROR "deps.cmake has no HC_DEP_${_dep}_LICENSE entry; every restored source needs its license file.")
    endif()
    set(_license "${_src}/${HC_DEP_${_dep}_PATH}/${HC_DEP_${_dep}_LICENSE}")
    if(NOT EXISTS "${_license}")
        message(FATAL_ERROR "License file for ${_dep} not found: ${HC_DEP_${_dep}_PATH}/${HC_DEP_${_dep}_LICENSE}")
    endif()
    file(READ "${_license}" _text)
    file(APPEND "${_copyright}" "\n==== ${_dep} (${HC_DEP_${_dep}_PATH}/${HC_DEP_${_dep}_LICENSE}) ====\n\n${_text}\n")
endforeach()
