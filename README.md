# HarmonEyes Theia SDK for C++

C++ SDK for real-time eye tracking analysis, cognitive load prediction and
fatigue detection.

This is the C++ counterpart to the Python SDK at
[HarmonEyesTheia](https://github.com/harmoneyes/HarmonEyesTheia), and exposes
the same public surface.

## Requirements

- A C++17 compiler
- CMake 3.16 or newer
- [Git LFS](https://git-lfs.com) (the prebuilt libraries are stored with it)

## Supported platforms

| Platform | Architecture |
|---|---|
| Linux | x86_64, aarch64 |
| macOS | Apple Silicon (arm64) |
| Windows | x86_64 |

Intel Macs are not supported.

## Installing

Clone the repository and pull the binaries:

```bash
git clone https://github.com/harmoneyes/HarmonEyesNative.git
cd HarmonEyesNative
git lfs install
git lfs pull
```

## Using it

Point `find_package` at the clone. There is no build or install step: the
repository is the package.

```cmake
find_package(HarmonEyesNative REQUIRED
             PATHS /path/to/HarmonEyesNative)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE HarmonEyesNative::theia)

# Puts the shared library next to the executable and sets a relative rpath.
harmoneyes_native_copy_runtime(my_app)
```

## Licensing

The SDK authenticates with a **device lease**: a signed, time-boxed token
verified entirely offline, so it works on machines with no connection back to
HarmonEyes. Set `THEIA_LEASE` in the environment, or pass the token directly
when constructing the SDK.

An invalid, expired or missing lease is reported immediately at construction,
before any device or model work happens.
