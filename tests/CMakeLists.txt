# Tier 1: no hardware, no network, no lease.
#
# Every test here guards a contract that breaks silently rather than loudly,
# which is why they exist at all.

set(HEN_TESTS
    test_types
    test_nexus_builder
    test_protocol
    test_license
    test_replay)

foreach(test IN LISTS HEN_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE HarmonEyesNative::theia)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
    harmoneyes_native_copy_runtime(${test})
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# Drives the sidecar process layer against a stub that enforces the real
# sidecar's contract. Needs python3; skips itself cleanly without it.
add_executable(test_webcam_lifecycle test_webcam_lifecycle.cpp)
target_link_libraries(test_webcam_lifecycle PRIVATE HarmonEyesNative::theia)
target_include_directories(test_webcam_lifecycle PRIVATE ${CMAKE_CURRENT_LIST_DIR})
harmoneyes_native_copy_runtime(test_webcam_lifecycle)
add_test(NAME test_webcam_lifecycle
         COMMAND test_webcam_lifecycle ${CMAKE_CURRENT_LIST_DIR}/fake_sidecar.py)
