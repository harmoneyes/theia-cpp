# HarmonEyes Theia SDK for C++

C++ SDK for real-time eye tracking analysis, cognitive load prediction and
fatigue detection.

This is the C++ counterpart to the Python SDK at
[HarmonEyesTheia](https://github.com/harmoneyes/HarmonEyesTheia) and exposes the
same public surface.

## Requirements

- A C++17 compiler
- CMake 3.16 or newer
- [Git LFS](https://git-lfs.com), since the prebuilt libraries are stored with it
- Node.js 20 or newer, **only** if you use the webcam gaze source

## Supported platforms

| Platform | Architecture |
|---|---|
| Linux | x86_64, aarch64 |
| macOS | Apple Silicon (arm64) |
| Windows | x86_64 |

Intel Macs are not supported.

## Installing

```bash
git clone https://github.com/harmoneyes/HarmonEyesNative.git
cd HarmonEyesNative
git lfs install
git lfs pull
```

There is no build or install step. The repository is the package.

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
HarmonEyes.

```cpp
harmoneyes::theia::Options options;
options.lease_token = std::getenv("THEIA_LEASE");
```

An invalid, expired or missing lease is reported when the SDK is constructed,
before any device or model work happens:

```cpp
try {
    harmoneyes::theia::TheiaSDK sdk{options};
} catch (const harmoneyes::theia::LeaseExpiredError& e) {
    // Mint a new one. A long-running process can swap it in with
    // sdk.set_lease_token(...) rather than restarting.
}
```

## Getting results

For Pupil Labs Neon, Ganzin and Tobii G3 the library connects to the device
itself:

```cpp
namespace het = harmoneyes::theia;

het::Options options;
options.config.device = het::DeviceType::PupilLabsNeon;
options.lease_token   = std::getenv("THEIA_LEASE");
options.on_cog_load   = [](const het::CogLoadResult& r) {
    std::cout << "cognitive load " << static_cast<int>(r.level)
              << " (confidence " << r.confidence << ")\n";
};

het::TheiaSDK sdk{options};
auto session = sdk.start_session("my-session-uuid");
sdk.start_data_streaming();
```

Results can also be polled. The getters are **consuming**: each returns the
latest result and clears it, so a second call before the next window returns
nothing.

```cpp
if (auto result = sdk.cog_load()) {
    use(result->level, result->confidence);
}
```

## Supplying your own samples

Webcam and replay sources push samples in. Implement `EyeTracker` and let a
`TrackerPump` drain it:

```cpp
class MyTracker : public het::EyeTracker {
public:
    std::vector<het::GazeSample> get_buffered_data() override {
        auto sample = het::make_gaze_sample();   // NOT GazeSample{}
        sample.timestamp_ns = ...;
        sample.gaze_x_norm  = ...;
        return {sample};
    }
    bool is_connected() const override { return true; }
};

MyTracker tracker;
het::TrackerPump pump{sdk, tracker};
pump.start();   // stops and joins when the pump goes out of scope
```

**Always start from `make_gaze_sample()`.** A value-initialised `GazeSample{}`
is not a neutral starting point: several fields mean "absent" at a non-zero
value, and `blink_id` in particular means "no blink" at `-1`, so a zeroed
sample claims to be part of blink 0 and the pipeline discards it.

## Webcam gaze

The library consumes gaze; it does not estimate it from a camera. That is done
by the Tobii Nexus engine, hosted by a Node sidecar that this SDK launches:

```cpp
namespace hn = harmoneyes::theia::nexus;

hn::WebcamSource::Config config;
config.license     = hn::TobiiLicense::from_env();
config.sidecar_dir = "/path/to/sidecar";

hn::WebcamSource camera{config};
camera.start();

het::TrackerPump pump{sdk, camera};
pump.start();
```

`sidecar_dir` must contain `sidecar.mjs`, and the Tobii engine's WASM must sit
under it in either `tobii/` or `node_modules/tobii_nexus_web/dist/`. Both are
checked before the sidecar is launched, so a missing bundle is reported
directly rather than as a startup failure from the child process. Set
`Config::tobii_package_dir` if the WASM lives somewhere else.

This needs Node 20+ on `PATH` and a Tobii engine licence, which is separate
from the HarmonEyes device lease. An air-gapped machine cannot obtain the
engine licence, so webcam tracking is unavailable there.

## Threading

- `add_gaze_samples()` is **single writer**. Call it from one thread at a time,
  and never while `start_data_streaming()` is feeding the pipeline itself.
  `TrackerPump` already honours this.
- Callbacks fire on the SDK's own threads, not yours. Anything they touch needs
  its own synchronisation.
- A `TheiaSDK` must not be moved while a `TrackerPump` is running against it.

## Replaying a recording

`DefaultTracker` replays the SDK's own `sdk_input_*.csv` files, which is how to
exercise the pipeline with no hardware:

```cpp
het::DefaultTracker::Options options;
options.input_dir        = "path/to/sdk_input";
options.simulate_realtime = true;    // release samples at their original rate

het::DefaultTracker replay{options};
replay.load();

het::TrackerPump pump{sdk, replay};
pump.start();
```

`load()` reads every `sdk_input_*.csv` in the directory, in name order, and
throws if there are none: a replay that silently produces nothing is worse than
a clear failure. Other files in a session directory, such as `imu_*.csv`, are
ignored.

Columns are matched by header name, so a file with extra or reordered columns
still loads, and anything the header does not name keeps its default.

## Environment variables

Every one of these is optional; each has an equivalent in code.

| Variable | Read by | Instead of |
|---|---|---|
| `THEIA_LEASE` | your code, as the lease token | `Options::lease_token` |
| `SDK_INPUT_DIR` | `DefaultTracker` | `DefaultTracker::Options::input_dir` |
| `THEIA_NODE` | `WebcamSource` | `Config::node_path` |
| `THEIA_WEBCAM_SIDECAR_DIR` | `WebcamSource` | `Config::sidecar_dir` |
| `THEIA_TOBII_LICENSE` | `TobiiLicense::from_env()` | `TobiiLicense::from_string()` |
| `THEIA_TOBII_LICENSE_FILE` | `TobiiLicense::from_env()` | `TobiiLicense::from_file()` |
| `THEIA_MODEL_CACHE_DIR` | the native library | `Options::model_cache_dir` |

`TobiiLicense::from_env()` prefers `THEIA_TOBII_LICENSE`, then
`THEIA_TOBII_LICENSE_FILE`, and throws naming both if neither is set.

## Things worth knowing

**On POSIX this library ignores `SIGPIPE`, process-wide.** `WebcamSource`
writes to a child process, and without that a child exiting at the wrong moment
would kill the host instead of returning an error. It is installed once, the
first time a sidecar is launched. If your application relies on `SIGPIPE`'s
default behaviour, handle it before starting a `WebcamSource`.

**Numbers are parsed independently of your locale.** Both the CSV reader and
the sidecar protocol read C-locale decimals regardless of `LC_NUMERIC`, so a
comma-decimal locale cannot silently turn `1.5` into `1.0`.

## What is not here

`DefaultTrackerVideo` from the Python SDK is absent: it wraps OpenCV, which
this SDK does not depend on. Video frames are reachable through the C entry
point `theia_sdk_get_video_frame()` in the vendored header; there is no C++
wrapper for it yet.

`DirectUploader` is absent because the Python class is an empty compatibility
stub with no methods.
