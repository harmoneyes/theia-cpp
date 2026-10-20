cmake_minimum_required(VERSION 3.16)

project(HarmonEyesNative
    VERSION 0.1.0
    DESCRIPTION "HarmonEyes Theia SDK for C++"
    LANGUAGES CXX)

# This project builds nothing of its own. The SDK is the vendored Theia library
# plus the header-only C++ layer in include/. Everything here exists only so the
# bundled examples and tests can be built from a clone.
#
# Customers do not build this repository. They point find_package at it:
#
#   find_package(HarmonEyesNative REQUIRED PATHS /path/to/HarmonEyesNative)
#   target_link_libraries(my_app PRIVATE HarmonEyesNative::theia)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_hen_is_top TRUE)
else()
    set(_hen_is_top FALSE)
endif()

option(HARMONEYES_NATIVE_BUILD_EXAMPLES "Build the bundled examples" ${_hen_is_top})
option(HARMONEYES_NATIVE_BUILD_TESTS    "Build the test suite"       OFF)

# Deliberately via find_package rather than a direct include: building from a
# clone then exercises the same discovery path a customer uses, so a broken
# config file fails here instead of in their project.
find_package(HarmonEyesNative REQUIRED CONFIG
             PATHS "${CMAKE_CURRENT_LIST_DIR}"
             NO_DEFAULT_PATH)

if(HARMONEYES_NATIVE_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(HARMONEYES_NATIVE_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

unset(_hen_is_top)
