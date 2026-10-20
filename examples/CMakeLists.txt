# One self-contained program per device, mirroring the Python SDK's examples.
set(HARMONEYES_EXAMPLES
    theia-pupil-labs-streaming
    theia-pupil-labs-batch
    theia-ganzin-streaming
    theia-tobii-g3
    theia-webcam-streaming
)

foreach(example IN LISTS HARMONEYES_EXAMPLES)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE HarmonEyesNative::theia)
    harmoneyes_native_copy_runtime(${example})
endforeach()
