# HarmonEyes Theia SDK for C++

C++ SDK for real-time eye tracking analysis, cognitive load prediction, and fatigue detection.

> This repository distributes **compiled binaries** of the HarmonEyes Theia SDK
> plus the C++ headers that call them. No source ships here.

## Installation

```bash
git lfs install            # once per machine; the binaries are stored with Git LFS
git clone https://github.com/harmoneyes/HarmonEyesNative.git
```

There is no build or install step: point CMake at the clone.

```cmake
find_package(HarmonEyesNative REQUIRED PATHS /path/to/HarmonEyesNative)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE HarmonEyesNative::theia)

# Puts the shared library next to your executable.
harmoneyes_native_copy_runtime(my_app)
```

## Requirements

- **A C++17 compiler** and **CMake 3.16+**.
- **[Git LFS](https://git-lfs.com)**, to fetch the binaries.
- **Platforms:** Linux x86_64 and aarch64 (arm64) with glibc 2.35 or later,
  macOS 14 or later on Apple silicon, and Windows x86_64. CMake reports
  clearly if your platform is not in a release.
- **On Linux,** the system's libcurl 4 and OpenSSL 3. Ubuntu 22.04+ and
  Debian 12+ ship them; minimal and container images may not, so install them
  with `apt install libcurl4 libssl3`.
- **On Windows,** the Microsoft Visual C++ Redistributable (2015-2022, x64).
  Most machines already have it; otherwise it is a free download from
  Microsoft.
- **Device Lease:** a valid device lease is required to use the SDK — see below.

## Device Lease

The SDK authenticates with a **device lease**: a signed, time-boxed token
verified entirely offline, so a customer machine never calls home to log in.
(It does still reach the model CDN; see
[Environment variables](#environment-variables) if you need to allowlist or
redirect that.)

**Getting your first lease:** contact support@harmoneyes.com. HarmonEyes staff
issues a lease for your device and sends you the token.

**Using it:** set `THEIA_LEASE` in your environment and the SDK picks it up, or
set `Options::lease_token` in code. `THEIA_LICENSE_KEY` is accepted too.

```bash
export THEIA_LEASE="<the token you were given>"
```

An invalid, expired, or missing lease throws `LeaseVerificationError` (or a
more specific subclass such as `LeaseExpiredError`) when constructing
`TheiaSDK`, before any device or model work happens. **Leases expire** —
request a new one the same way you got the first; a long-running process can
swap it in with `sdk.set_lease_token(new_token)`.

## Quick start

```cpp
#include <harmoneyes/theia.hpp>

namespace het = harmoneyes::theia;

het::TheiaSDK sdk{het::Device::PupilLabsNeon};
sdk.start_new_session(session_uuid);
sdk.start_realtime_data();

// once a second:
if (auto cog = sdk.get_cog_load())          { std::cout << cog->label << '\n'; }  // Low / Moderate / High
if (auto fatigue = sdk.get_fatigue_level()) { std::cout << fatigue->label << '\n'; }

sdk.stop_processing();
```

See [`examples/quickstart.cpp`](examples/quickstart.cpp). The whole API, with a
line of documentation per call, is in
[`harmoneyes/theia.hpp`](include/harmoneyes/theia.hpp).

Failures are reported as exceptions: catch `std::exception` to handle them
all, or `het::LeaseVerificationError` for lease problems. Use a `TheiaSDK` from
one thread at a time. Destroying it stops everything.

## Results

| Call | Returns | Updates |
|---|---|---|
| `get_cog_load()` | `level` (0=Low, 1=Moderate, 2=High), `label`, `confidence` | about once a second, after a ~30 s warmup |
| `get_fatigue_level()` | `level` (0=Alert, 1=Somewhat Fatigued, 2=Fatigued), `label`, `confidence`, time-to-transition | about every 2 minutes |
| `get_attention_style()` | `level` (1=Narrow … 5=Broad), `label` | continuously |
| `get_mental_readiness(elapsed_seconds)` | `low_percentage`, `moderate_percentage`, `high_percentage` | continuously |

Each returns an empty `std::optional` until a result is available.
`get_cog_load()` and `get_fatigue_level()` return the newest result and clear
it, so poll them about once a second. To receive every result instead, set
`Options::on_cog_load` and `Options::on_fatigue`; those callbacks run on the
SDK's own threads.

## Platform Setup

### Pupil Labs Neon

The Neon is discovered on your network automatically.

```cpp
het::TheiaSDK sdk{het::Device::PupilLabsNeon};
```

See [`examples/theia-pupil-labs-streaming.cpp`](examples/theia-pupil-labs-streaming.cpp).

### Ganzin Sol

The Ganzin Sol connects over the network. Set its address before starting the
data stream.

| Setting | Default | Description |
|---------|---------|-------------|
| `ip`    | — | IP address of the Ganzin Sol device |
| `port`  | `8080` | port exposed by the device |

```cpp
het::TheiaSDK sdk{het::Device::Ganzin};
sdk.set_ganzin_connection("192.168.1.100", 8080);
```

> **Tip:** Make sure the Ganzin Sol is powered on and reachable at the
> configured IP before running.

See [`examples/theia-ganzin-streaming.cpp`](examples/theia-ganzin-streaming.cpp).

### Your own tracker

Two devices take gaze from a tracker you supply, rather than one the SDK
connects to itself:

| Device | For |
|---|---|
| `Device::Webcam` | screen-based trackers, about 30 samples a second |
| `Device::Headset` | head-mounted trackers that report where each eye points |

There are two ways to hand it the samples.

**Push them as they arrive** with `add_gaze_samples()`. This is the simpler
one when your tracker calls you back with each frame:

```cpp
het::TheiaSDK sdk{het::Device::Headset};
sdk.start_new_session(session_uuid);

// in your tracker's frame callback:
sdk.add_gaze_samples({sample});   // or a std::vector of several, in time order

// when the recording ends:
sdk.stop_processing();
```

There is no `start_realtime_data()` call. `add_gaze_samples()` may run on
another thread, such as that callback, while you call the `get_*` methods, but
not alongside any other call, itself included. Most calls return in
microseconds; about once a second one takes a few milliseconds to score the
newest data, so keep it off render and UI threads.

**Or let the SDK poll you:** implement `het::EyeTracker`, returning the samples
gathered since the last call from `get_buffered_data()`, and hand it to
`set_tracker()` before starting the stream.

```cpp
MyTracker     tracker;        // implements het::EyeTracker; declare it FIRST
het::TheiaSDK sdk{het::Device::Headset};
sdk.set_tracker(tracker);
sdk.start_new_session(session_uuid);
sdk.start_realtime_data();    // calls tracker.start(), then polls it
```

`start_realtime_data()` calls `start()`, then polls `get_buffered_data()` and
`is_connected()` on the SDK's own thread; `stop_processing()` stops polling and
calls `close()`. The SDK never calls two of these at once, so your tracker can
share state between them without locking. Samples you collect on another
thread, such as your tracker's own callback, do need a lock.

**Lifetime:** the tracker must outlive the SDK, and cannot be swapped while
running. Declaring it *after* the SDK destroys it first, which is the easy way
to get this wrong, so declare it first as above. If it cannot outlive the SDK,
call `clear_tracker()` before destroying it.

#### Head-mounted: `Device::Headset`

`Device::Headset` reads three things from each `het::GazeSample` and ignores
the rest:

| Field | Set it to |
|---|---|
| `timestamp_ns` | the capture time, in nanoseconds since the Unix epoch |
| `optical_axis_left_x/y/z`, `optical_axis_right_x/y/z` | each eye's gaze direction, as a unit vector in the head's frame: +x right, +y up, +z forward. Leave an eye at `(0, 0, 0)` when it was not tracked; one eye is enough. |
| `pupil_diameter_left`, `pupil_diameter_right` | the pupil diameters, in millimetres |

- **Check the y axis.** Many eye cameras count +y downwards; negate it if
  yours does.
- **Send pupil diameters if you can.** Blinks are detected from the pupil
  signal. Without it the SDK finds none, and cognitive load reads Low
  throughout.
- **It is tuned for Tobii Pro Glasses 3,** at 50 samples a second. Results
  from other headsets have not been validated, so contact
  support@harmoneyes.com before relying on them.

#### Screen-based: `Device::Webcam`

Set `gaze_x_norm` and `gaze_y_norm` on every sample: the angular features are
computed from those, not from pixels. Results from trackers that differ much
from about 30 samples a second have not been validated, so contact
support@harmoneyes.com before relying on them.

A default-constructed `het::GazeSample` uses `NON_COMPUTE_VALUE` for the
optional measurements (pupil, eyelid, IMU, normalised gaze, screen and head
pose), so you can leave those alone. It does **not** mark everything absent:
eye positions default to `0.0`, and `left_eye_validity` / `right_eye_validity`
default to `1` (valid) with `left_eye_status` / `right_eye_status` at `2`
(open). Set those yourself on any sample where your tracker lost the eye,
otherwise a dropped frame is fed in as a good one.

## Coming from the Python SDK

The method names are the same. The differences are the ones C++ makes natural:

| Python | C++ |
|---|---|
| `TheiaSDK(lease_token=..., platform="PL")` | `TheiaSDK{Device::PupilLabsNeon}` (lease from `THEIA_LEASE`), or `TheiaSDK{options}` |
| `get_cog_load_levels()` → `(dict, batch, lookahead)` | `get_cog_load()` → `std::optional<CogLoadResult>` |
| `get_fatigue_level()` → `(dict, batch)` | `get_fatigue_level()` → `std::optional<FatigueResult>` |
| `get_attention_style()` / `get_mental_readiness(t)` → `dict` or `None` | same names → `std::optional<…>` |
| `sdk.ip = ...; sdk.port = ...` | `sdk.set_ganzin_connection(ip, port)` |
| `sdk.tracker.set_tracker(...)` | `sdk.set_tracker(tracker)` |
| `except LeaseExpiredError` | `catch (const het::LeaseExpiredError&)` |

**Some features are Python-only for now:** the webcam
(`NexusWebcamTracker`), and recorded data: `predict_cog_load_batch()`,
`predict_fatigue_batch()` and `process_tobii_g3_data()` (Tobii Pro Glasses 3). Calls the Python SDK keeps only for older code,
such as `set_window_size()` and `set_profile()`, are left out.

## Tuning and personalisation

| Call | What it does |
|---|---|
| `set_mental_readiness_min_session_seconds` | how long a session must run before mental readiness reports |
| `set_fatigue_time_offset_seconds` | shifts the clock the fatigue model reads time of day from |
| `set_cog_load_model_settings` | smoothing window and confidence thresholds (`CogLoadModelSettings`) |

To personalise the cognitive-load model to one person, set the session's time
origin with `set_graph_start_time()`, then use `process_baselining_data()`,
`process_general_model_baseline()` and `process_refining_data()` with labelled
`GroundTruthPoint` values. Keep the result with `save_refined_model()` and
`load_refined_model()`, or go back with `reset_to_base_model()`.

Alert rules are configured with `set_alert_rules()`, delivered to
`set_alert_callback()`, and can be kept in sync with your HarmonEyes dashboard
with `enable_alert_sync()`.

`set_alert_rules()` takes a JSON document, not a bare array; anything else is
rejected:

```json
{ "format": "theia-alert-rules", "version": 1, "etag": "\"v1\"", "rules": [] }
```

## Environment variables

All optional. Those marked — can only be set through the environment.

| Variable | Used for | In code |
|---|---|---|
| `THEIA_LEASE` (or `THEIA_LICENSE_KEY`) | your device lease | `Options::lease_token` |
| `THEIA_MODEL_CACHE_DIR` | where downloaded models are cached | `Options::model_cache_dir` |
| `THEIA_MODEL_CDN_URL` | where models are downloaded from | — |
| `THEIA_DEVICE_API_URL` | the HarmonEyes API used by `enable_alert_sync()` | its `api_base_url` |
| `THEIA_ALERT_CACHE_DIR` | where synced alert rules are cached | — |

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
