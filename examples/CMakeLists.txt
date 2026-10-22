# One self-contained program per device, mirroring the Python SDK's examples,
# plus the smallest complete program.
set(HARMONEYES_EXAMPLES
    quickstart
    theia-pupil-labs-streaming
    theia-ganzin-streaming
)

foreach(example IN LISTS HARMONEYES_EXAMPLES)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE HarmonEyesNative::theia)
    harmoneyes_native_copy_runtime(${example})
endforeach()
