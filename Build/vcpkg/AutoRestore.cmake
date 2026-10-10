# Restores libHttpClient's third-party sources into External/ at configure time when they are
# missing or out of date (see Build/vcpkg/Restore-Vcpkg.ps1 / restore-vcpkg.sh).
# Include this before anything references External/. Set HC_SKIP_VCPKG_RESTORE=1 (environment or
# CMake cache) to opt out, for example offline builds or consumers that stage External/ themselves.
include_guard(GLOBAL)

get_filename_component(_hc_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_hc_deps_file "${CMAKE_CURRENT_LIST_DIR}/ports/libhttpclient-deps/deps.cmake")
set(_hc_deps_stamp "${_hc_root}/External/.vcpkg-deps.stamp")

# Re-run configure (and so this check) whenever the pins change.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_hc_deps_file}")

set(_hc_skip OFF)
if(HC_SKIP_VCPKG_RESTORE OR "$ENV{HC_SKIP_VCPKG_RESTORE}" STREQUAL "1")
    set(_hc_skip ON)
endif()

set(_hc_current OFF)
if(EXISTS "${_hc_deps_stamp}")
    file(READ "${_hc_deps_file}" _hc_deps_text)
    file(READ "${_hc_deps_stamp}" _hc_stamp_text)
    if(_hc_deps_text STREQUAL _hc_stamp_text)
        set(_hc_current ON)
    endif()
endif()

if(NOT _hc_skip AND NOT _hc_current)
    message(STATUS "libHttpClient: restoring third-party sources into ${_hc_root}/External")
    if(CMAKE_HOST_WIN32)
        execute_process(
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/Restore-Vcpkg.ps1"
            RESULT_VARIABLE _hc_restore_result)
    else()
        execute_process(
            COMMAND bash "${CMAKE_CURRENT_LIST_DIR}/restore-vcpkg.sh"
            RESULT_VARIABLE _hc_restore_result)
    endif()
    if(NOT _hc_restore_result EQUAL 0)
        message(FATAL_ERROR
            "libHttpClient: restoring third-party sources failed (${_hc_restore_result}). "
            "Run Build/vcpkg/Restore-Vcpkg.ps1 (Windows) or Build/vcpkg/restore-vcpkg.sh to see details, "
            "or set HC_SKIP_VCPKG_RESTORE=1 if External/ is populated another way.")
    endif()
endif()
