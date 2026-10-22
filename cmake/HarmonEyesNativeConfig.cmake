# HarmonEyesNativeConfig.cmake
#
# Makes the vendored Theia native library consumable as:
#
#   find_package(HarmonEyesNative REQUIRED PATHS /path/to/HarmonEyesNative)
#   target_link_libraries(my_app PRIVATE HarmonEyesNative::theia)
#   harmoneyes_native_copy_runtime(my_app)
#
# This file is used in place, straight from a clone. There is no build or
# install step: the package IS the repository. The supported platforms are
# the same as the Python SDK's.

include(CMakeFindDependencyMacro)

get_filename_component(HARMONEYES_NATIVE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# --- platform detection ------------------------------------------------------

# CMAKE_SYSTEM_PROCESSOR is unreliable on Apple (universal builds) and MSVC,
# so prefer the explicit knobs where they exist.
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _hen_osx_arch_count)
    if(_hen_osx_arch_count GREATER 1)
        message(FATAL_ERROR
            "HarmonEyesNative: universal builds are not supported. "
            "CMAKE_OSX_ARCHITECTURES is '${CMAKE_OSX_ARCHITECTURES}'; set it to arm64.")
    endif()
    set(_hen_arch "${CMAKE_OSX_ARCHITECTURES}")
else()
    set(_hen_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(_hen_implib "")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The library carries SONAME "libtheia.so.0", so that is the name an
    # executable records in DT_NEEDED and the name the loader searches for at
    # runtime. Vendor and reference it under the SONAME, not "libtheia.so".
    if(_hen_arch MATCHES "^(x86_64|AMD64|amd64)$")
        set(_hen_platform "linux-x86_64")
        set(_hen_libname  "libtheia.so.0")
    elseif(_hen_arch MATCHES "^(aarch64|arm64)$")
        set(_hen_platform "linux-aarch64")
        set(_hen_libname  "libtheia.so.0")
    else()
        message(FATAL_ERROR
            "HarmonEyesNative: unsupported Linux architecture: ${_hen_arch}. "
            "Only x86_64 and aarch64 are supported.")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(_hen_arch MATCHES "^(arm64|aarch64)$")
        set(_hen_platform "macos-arm64")
        set(_hen_libname  "libtheia.dylib")
    else()
        message(FATAL_ERROR
            "HarmonEyesNative: Intel Macs (x86_64) are not currently supported. "
            "Only Apple Silicon (arm64) is supported.")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(_hen_arch MATCHES "^(AMD64|x86_64|x64)$")
        set(_hen_platform "windows-x86_64")
        set(_hen_libname  "theia.dll")
        set(_hen_implib   "theia.lib")
    else()
        message(FATAL_ERROR
            "HarmonEyesNative: unsupported Windows architecture: ${_hen_arch}. "
            "Only x86_64 is supported.")
    endif()
else()
    message(FATAL_ERROR
        "HarmonEyesNative: unsupported operating system: ${CMAKE_SYSTEM_NAME}. "
        "Supported: Linux (x86_64, aarch64), macOS (arm64), Windows (x86_64).")
endif()

set(HarmonEyesNative_PLATFORM "${_hen_platform}")

set(_hen_libdir  "${HARMONEYES_NATIVE_ROOT}/vendor/${_hen_platform}")
set(_hen_libpath "${_hen_libdir}/${_hen_libname}")
set(_hen_includes "${HARMONEYES_NATIVE_ROOT}/include" "${HARMONEYES_NATIVE_ROOT}/vendor/include")

# The binaries are stored with Git LFS. A clone without it leaves a ~130-byte
# text pointer in place of each one, so EXISTS alone cannot tell "absent"
# from "not fetched".
function(_hen_require_binary path what)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "HarmonEyesNative: this release does not include the ${what} for "
            "${HarmonEyesNative_PLATFORM}.\n"
            "  expected: ${path}\n"
            "Contact support@harmoneyes.com if you need this platform.")
    endif()
    file(SIZE "${path}" _size)
    if(_size LESS 4096)
        message(FATAL_ERROR
            "HarmonEyesNative: ${path} is a Git LFS pointer, not the real file.\n"
            "Run: git lfs install && git lfs pull")
    endif()
endfunction()

_hen_require_binary("${_hen_libpath}" "library")

# Windows cannot link a bare DLL; CMake requires the import library.
if(_hen_implib)
    set(_hen_implibpath "${_hen_libdir}/${_hen_implib}")
    _hen_require_binary("${_hen_implibpath}" "import library (${_hen_implib})")
endif()

# --- the imported target -----------------------------------------------------

if(NOT TARGET HarmonEyesNative::theia)
    add_library(HarmonEyesNative::theia SHARED IMPORTED GLOBAL)

    set_target_properties(HarmonEyesNative::theia PROPERTIES
        IMPORTED_LOCATION             "${_hen_libpath}"
        INTERFACE_INCLUDE_DIRECTORIES "${_hen_includes}"
        INTERFACE_COMPILE_FEATURES    cxx_std_17)

    if(_hen_implib)
        set_target_properties(HarmonEyesNative::theia PROPERTIES
            IMPORTED_IMPLIB "${_hen_implibpath}")
    endif()

    if(UNIX)
        # The library needs these three at link time on every Unix.
        find_dependency(Threads)
        set_property(TARGET HarmonEyesNative::theia APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()
endif()

# --- runtime placement helper ------------------------------------------------

# Puts the shared library next to the executable and sets a relative rpath,
# so the build tree runs with no further setup.
#
# This covers the BUILD tree only. To ship the library with an installed
# binary:
#   install(FILES $<TARGET_FILE:HarmonEyesNative::theia> DESTINATION bin)
function(harmoneyes_native_copy_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:HarmonEyesNative::theia> $<TARGET_FILE_DIR:${target}>
        COMMENT "Copying libtheia next to ${target}")

    if(APPLE)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@loader_path")
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET ${target} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
endfunction()

unset(_hen_arch)
unset(_hen_platform)
unset(_hen_libname)
unset(_hen_implib)
unset(_hen_libdir)
unset(_hen_libpath)
unset(_hen_implibpath)
unset(_hen_includes)
unset(_hen_osx_arch_count)
