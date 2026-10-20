# HarmonEyes Theia SDK for C++

C++ SDK for real-time eye tracking analysis, cognitive load prediction, and fatigue detection.

> This repository distributes **compiled binaries** of the HarmonEyes Theia SDK
> plus the headers that call them. No engine source ships here.

> **Coming from the Python SDK?** See [`MIGRATION.md`](MIGRATION.md) for the
> name-by-name mapping and the differences C++ forces.

## Installation

```bash
git clone https://github.com/harmoneyes/HarmonEyesNative.git
cd HarmonEyesNative
git lfs install
git lfs pull
```

There is no build or install step. The repository is the package.

```cmake
find_package(HarmonEyesNative REQUIRED PATHS /path/to/HarmonEyesNative)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE HarmonEyesNative::theia)

# Puts the shared library next to the executable and sets a relative rpath.
harmoneyes_native_copy_runtime(my_app)
```

## Requirements

- **A C++17 compiler** and **CMake 3.16+**. The SDK is header-only, so it
  compiles in your toolchain; only the engine is prebuilt.
- **[Git LFS](https://git-lfs.com)** — the engine binaries are stored with it.
  Without `git lfs pull` you get 130-byte text pointers and a clear error.
- **Platforms:** Linux x86_64 ships today. Linux aarch64, macOS arm64 and
  Windows x86_64 are not yet vendored; `find_package` fails naming the
  platform. Intel Macs are not supported.
- **Node.js 20+** — required **only for the Webcam (Tobii Nexus) platform**,
  whose gaze sidecar runs on Node. Not needed for Pupil Labs, Ganzin, batch, or
  Tobii G3.
- **Device Lease:** a valid device lease is required to use the SDK — see below.

## Device Lease

This SDK runs on customer machines with no connection back to HarmonEyes, so it
authenticates with a **device lease** instead of a live login: a signed,
time-boxed token that's verified entirely offline, rather than a plain license
key checked against a server.

**Getting your first lease:** contact sales@harmoneyes.com. HarmonEyes staff
issues a lease for your device and sends you the token, so you don't need to
request or manage any credentials yourself.

**Using it:** set `THEIA_LEASE` in your environment and read it, or set
`Options::lease_token` directly. The older `THEIA_LICENSE_KEY` name is still
accepted by the examples if you already have code using it.

```bash
export THEIA_LEASE="<the token you were given>"
```

```cpp
namespace het = harmoneyes::theia;

het::Options options;
options.lease_token   = std::getenv("THEIA_LEASE");
options.config.device = het::DeviceType::PupilLabsNeon;

het::TheiaSDK sdk{options};
```

An invalid, expired, or missing lease throws `het::LeaseVerificationError` (or
a more specific subclass — `LeaseExpiredError`, `LeaseMalformedError`, etc.)
immediately when constructing `TheiaSDK`, before any device or model work
happens. **Leases expire** — when yours is nearing expiry, request a new one
the same way you got the first; a long-running process can swap it in without
restarting via `sdk.set_lease_token(new_token)`.

```cpp
try {
    het::TheiaSDK sdk{options};
} catch (const het::LeaseExpiredError&) {
    // Request a new one, then sdk.set_lease_token(...).
} catch (const het::Error& e) {
    // Catch the base too. The lease is not the only thing checked at
    // construction: a model that is neither cached nor reachable raises
    // ModelUnavailableError from the same call.
    std::cerr << e.what() << '\n';
}
```

## Getting results

Results are polled, and **two of the four getters consume, two do not.** The
library decides this, not the wrapper.

`cog_load()` and `fatigue()` are event streams: each returns the latest result
and clears it, so a second call before the next window returns nothing.

`attention()` and `mental_readiness()` are rolling state: they return the
current classification every time, and clear nothing. Reading the same value
twice means no new window has landed, not that a new window agreed with the
last one.

```cpp
auto session = sdk.start_session("my-session-uuid");
sdk.start_data_streaming();

if (auto cog = sdk.cog_load()) {
    use(cog->level, cog->confidence);   // 0=Low, 1=Moderate, 2=High
}
if (auto fatigue = sdk.fatigue()) {
    use(fatigue->level);                // 0=Alert, 1=Somewhat Fatigued, 2=Fatigued
}
```

You can take them by callback instead, via `Options::on_cog_load` and
`Options::on_fatigue`. Those fire on the SDK's own threads and never drop a
window, which is what you want when scoring a recording rather than driving a
live display.

Dropping the `Session` stops the session, and stopping a session also stops
data streaming.

## Platform Setup

### Pupil Labs Neon

The library discovers the Neon over mDNS and owns the connection, so there is
no tracker to construct.

```cpp
het::Options options;
options.lease_token   = std::getenv("THEIA_LEASE");
options.config.device = het::DeviceType::PupilLabsNeon;

het::TheiaSDK sdk{options};
auto session = sdk.start_session(uuid);
sdk.start_data_streaming();
```

See [`examples/theia-pupil-labs-streaming.cpp`](examples/theia-pupil-labs-streaming.cpp).

### Ganzin Sol

The Ganzin Sol connects over the network and has no discovery, so set its
address before starting a session.

| Argument | Default | Description |
|---------|---------|-------------|
| `ip` | none, required | IP address of the Ganzin Sol |
| `port` | `8080` | HTTP control port |
| `rtsp_port` | `8086` | gaze and video stream port |

```cpp
options.config.device = het::DeviceType::Ganzin;
het::TheiaSDK sdk{options};

sdk.set_ganzin_connection("192.168.1.100", 8080, 8086);
```

> **Tip:** make sure the Sol is powered on and reachable at the configured IP
> before running.

See [`examples/theia-ganzin-streaming.cpp`](examples/theia-ganzin-streaming.cpp).

### Tobii Pro Glasses 3

The G3 advertises itself over mDNS, so the address is optional — pass one to
pick between several pairs. The gaze rate and calibration must both be settled
before the session starts.

```cpp
options.config.device = het::DeviceType::TobiiG3;
het::TheiaSDK sdk{options};

sdk.set_tobii_g3_connection();                     // or ("192.168.1.50")

// Both return whether the device accepted, and both are [[nodiscard]].
if (!sdk.set_tobii_g3_gaze_frequency(50)) { /* refused */ }
if (!sdk.calibrate_tobii_g3())            { /* reseat and retry */ }
```

> **Use 50 Hz.** The rate also selects which device model runs, and the
> library's legacy feature extractor fixes its rate when the SDK is
> constructed. The C ABI has no way to set the rate that early, so asking for
> 100 Hz afterwards leaves that extractor at 50 and skews eye-movement features
> with no error. `set_tobii_g3_gaze_frequency()` returns `false` rather than
> clamping anything that is not 50 or 100.

See [`examples/theia-tobii-g3.cpp`](examples/theia-tobii-g3.cpp).

### Webcam (Tobii Nexus)

The Webcam platform turns on your webcam and estimates gaze with the Tobii
Nexus engine. The library does not read the camera itself: `WebcamSource` spawns
a small Node sidecar that hosts the engine, and a `TrackerPump` feeds its
samples in.

```cpp
options.config.device = het::DeviceType::Nexus;
het::TheiaSDK sdk{options};

namespace hetn = harmoneyes::theia::nexus;
hetn::WebcamSource::Config camera;
camera.license              = hetn::TobiiLicense::from_env();
camera.screen_width_mm      = 336;    // set these to your actual display
camera.screen_height_mm     = 210;
camera.screen_res_width_px  = 1920;
camera.screen_res_height_px = 1080;

hetn::WebcamSource source{std::move(camera)};
source.start();                       // opens the camera, waits for readiness

auto session = sdk.start_session(uuid);
het::TrackerPump pump{sdk, source};
pump.start();
```

**Requirements:** **Node.js 20+** on your `PATH`, and a Tobii engine licence in
`THEIA_TOBII_LICENSE` or `THEIA_TOBII_LICENSE_FILE`. Fetching the licence over
HTTP is deliberately not built in, so a header-only SDK doesn't oblige you to
link an HTTP stack.

> Set the screen geometry to your real display. The engine maps gaze onto the
> screen with it, so the defaults bias every coordinate.

> You can supply your own gaze source instead: implement `EyeTracker` and hand
> it to a `TrackerPump`.

See [`examples/theia-webcam-streaming.cpp`](examples/theia-webcam-streaming.cpp).

### Batch processing (recorded data)

There is no batch call. Python's `predict_cog_load_batch()` drives a second
compiled extension that parses a vendor export into features; this SDK does not
ship that extension, so there is nothing here to call. Scoring a recording
means reading it into samples, feeding them in, and collecting the callbacks.
`DefaultTracker` does the reading for the SDK's own `sdk_input_*.csv` files.

```cpp
het::DefaultTracker::Options replay;
replay.input_dir         = "path/to/sdk_input";
replay.simulate_realtime = false;    // hand everything over at once

het::DefaultTracker tracker{replay};
tracker.load();
sdk.add_gaze_samples(tracker.get_buffered_data());
```

The `timestamp` column is **milliseconds**, which is what the SDK writes; a
`timestamp_ns` column is taken as nanoseconds. Columns are matched by name, so
extra or reordered columns still load, and `imu_*.csv` files in the same
directory are ignored.

See [`examples/theia-pupil-labs-batch.cpp`](examples/theia-pupil-labs-batch.cpp).

### Video

Trackers that provide a scene or eye camera expose it through four calls each.
Frames are pulled, not pushed, so `video_frame()` returns nothing until one is
ready.

```cpp
sdk.start_video();
if (auto frame = sdk.video_frame()) {
    use(frame->bgr_data, frame->width, frame->height);
}
sdk.stop_video();
```

`VideoFrame` owns its bytes. The C layer hands back pointers into a buffer the
next call overwrites, so the wrapper copies. `bgr_data` and `encoded_data` are
filled depending on what the tracker provides, and either may be empty.

`start_eye_video()`, `stop_eye_video()`, `is_eye_video_streaming()` and
`eye_video_frame()` are the same four calls for the eye camera.

## Tuning and session metadata

Beyond the per-device setup, the SDK takes a handful of settings that apply
from the next window:

| Call | What it does |
|---|---|
| `set_fatigue_time_offset_seconds` | shifts the clock the fatigue model reads time-of-day from; set it per session when replaying a recording captured at a different hour |
| `set_mental_readiness_min_session_seconds` | how long a session must run before readiness reports anything |
| `set_mental_readiness_low_high_threshold` | the low/high boundary |
| `set_mental_readiness_report_windows` | the nominal window lengths reported on |
| `set_window_size` | the analysis window, in seconds |
| `set_graph_start_time` | the session's wall-clock origin |
| `set_sdk_model_confidence_threshold` | the confidence floor for a prediction |
| `set_cog_load_model_settings` | consensus window, consensus threshold, and the two confidence thresholds |

`set_profile()`, `set_task()`, `set_environment()` and `set_lambda_url()`
record metadata alongside the results, readable back with `profile()`,
`task()` and `environment()`. `model_info()` reports which models are loaded
and at what version, and `cog_load_history()` returns every cognitive-load
prediction so far.

To personalise the cognitive-load model to one person, use
`process_baselining_data()`, `process_general_model_baseline()` and
`process_refining_data()` with labelled `GroundTruthPoint` values, tune with
`set_refine_settings()`, then `save_refined_model()` / `load_refined_model()`,
or `reset_to_base_model()` to go back.

## Environment variables

Every one is optional; each has an equivalent in code.

| Variable | Read by | Instead of |
|---|---|---|
| `THEIA_LEASE` | your code, as the lease token | `Options::lease_token` |
| `SDK_INPUT_DIR` | `DefaultTracker` | `DefaultTracker::Options::input_dir` |
| `THEIA_NODE` | `WebcamSource` | `Config::node_path` |
| `THEIA_WEBCAM_SIDECAR_DIR` | `WebcamSource` | `Config::sidecar_dir` |
| `THEIA_TOBII_LICENSE` | `TobiiLicense::from_env()` | `TobiiLicense::from_string()` |
| `THEIA_TOBII_LICENSE_FILE` | `TobiiLicense::from_env()` | `TobiiLicense::from_file()` |
| `THEIA_MODEL_CACHE_DIR` | the native library | `Options::model_cache_dir` |

## Things worth knowing

**Features need real eye data.** The pipeline computes nothing useful from
synthetic or pixel-only samples: the classifier marks them invalid and every
feature comes back as `NON_COMPUTE_VALUE` (`-8888.0`). A real recording
produces around 99% computed features; a hand-made sine wave produces 0%.

**Build gaze samples with `make_gaze_sample()`, not `GazeSample{}`.** A zeroed
sample is not a blank one: several fields have non-zero "absent" defaults, and
a zeroed `blink_id` marks every sample as a blink.

**A rejected alert-rules document does not say why.** `set_alert_rules()`
throws when the document is unreadable, but the library records no detail, so
the message cannot name the offending rule. A single unusable rule inside an
otherwise valid document is skipped rather than rejected, and
`alert_rules_status().skipped_detail` does report that.

**On POSIX this library ignores `SIGPIPE`, process-wide**, installed the first
time a sidecar launches. Without it a child exiting at the wrong moment would
kill the host. Handle `SIGPIPE` before starting a `WebcamSource` if you rely on
its default behaviour.

**Numbers are parsed independently of your locale.** Both the CSV reader and
the sidecar protocol read C-locale decimals regardless of `LC_NUMERIC`.

## What is not here

`DefaultTrackerVideo` from the Python SDK is absent: it wraps OpenCV, which
this SDK does not depend on. The underlying video stream is not missing, only
that convenience class. See [Video](#video) above.

`DirectUploader` is absent because the Python class is an empty compatibility
stub with no methods.

## License & Usage

This software is proprietary and requires a valid device lease to operate (see
[Device Lease](#device-lease) above). The compiled binaries are publicly
distributed but will not function without one.

- **Copyright**: © RightEye LLC / HarmonEyes
- **License Type**: Proprietary / Commercial
- **Obtaining a Device Lease**: contact sales@harmoneyes.com

## Support

For issues, questions, or feature requests, please contact support@harmoneyes.com
or create an issue on GitHub.
