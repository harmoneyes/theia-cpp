# HarmonEyesNativeConfigVersion.cmake
#
# Lets find_package(HarmonEyesNative 1.0 ...) check the release in
# vendor/VERSION ("v1.2.3"). A request is met by the same major version at
# that version or later, so 1.0 accepts any 1.x release.

set(PACKAGE_VERSION "0.0.0")
set(_hen_version_file "${CMAKE_CURRENT_LIST_DIR}/../vendor/VERSION")
if(EXISTS "${_hen_version_file}")
    file(READ "${_hen_version_file}" _hen_version)
    # "v1.2.3" or "v1.2.3-rc.1" -> "1.2.3"; a pre-release compares as its release.
    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" PACKAGE_VERSION "${_hen_version}")
endif()

if(NOT PACKAGE_FIND_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
else()
    string(REGEX MATCH "^[0-9]+" _hen_major "${PACKAGE_VERSION}")
    if(_hen_major STREQUAL PACKAGE_FIND_VERSION_MAJOR AND
       NOT PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
        set(PACKAGE_VERSION_COMPATIBLE TRUE)
        if(PACKAGE_VERSION VERSION_EQUAL PACKAGE_FIND_VERSION)
            set(PACKAGE_VERSION_EXACT TRUE)
        endif()
    else()
        set(PACKAGE_VERSION_COMPATIBLE FALSE)
    endif()
endif()

unset(_hen_version_file)
unset(_hen_version)
unset(_hen_major)
