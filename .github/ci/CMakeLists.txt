# CI only: builds requires_lease.cpp against the vendored release through
# find_package(), the way a customer's project consumes it, on every platform.
cmake_minimum_required(VERSION 3.16)
project(harmoneyes_native_ci LANGUAGES CXX)

find_package(HarmonEyesNative REQUIRED PATHS "${CMAKE_CURRENT_LIST_DIR}/../.." NO_DEFAULT_PATH)

add_executable(requires_lease requires_lease.cpp)
target_link_libraries(requires_lease PRIVATE HarmonEyesNative::theia)
# The check reads the environment with std::getenv, which MSVC warns about.
target_compile_definitions(requires_lease PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
harmoneyes_native_copy_runtime(requires_lease)
