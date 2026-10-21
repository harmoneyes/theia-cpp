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
- **Platforms:** Linux x86_64. Linux aarch64, macOS arm64 (Apple Silicon) and
  Windows x86_64 are coming; CMake reports clearly if your platform is not in
  this release.
- **Node.js 20+** — required **only for the Webcam platform**, whose gaze
  engine runs on Node. Install from [nodejs.org](https://nodejs.org) and ensure
  `node` is on your `PATH`.
- **Device Lease:** a valid device lease is required to use the SDK — see below.

## Device Lease

This SDK runs on customer machines with no connection back to HarmonEyes, so it
authenticates with a **device lease**: a signed, time-boxed token that's
verified entirely offline.

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
[`harmoneyes/theia.hpp`](include/harmoneyes/theia.hpp); the webcam adds
[`harmoneyes/theia_webcam.hpp`](include/harmoneyes/theia_webcam.hpp).

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

### Webcam

The Webcam platform turns on your webcam and estimates gaze with the bundled
Tobii Nexus engine. Give the SDK a `NexusWebcamTracker`; starting the data
stream opens the camera.

```cpp
#include <harmoneyes/theia.hpp>
#include <harmoneyes/theia_webcam.hpp>

het::TheiaSDK sdk{het::Device::Webcam};
het::NexusWebcamTracker webcam;
sdk.set_tracker(webcam);
sdk.start_new_session(session_uuid);
sdk.start_realtime_data();   // opens the camera and starts the gaze engine
```

**Requirements:** **Node.js 20+** on your `PATH`. The gaze engine's licence is
fetched automatically from the endpoint shipped with the SDK, which needs a
key: set `TOBII_LICENSE_API_KEY` (or `Config::license_api_key`). Without it the
endpoint answers 401 and the webcam will not start.

`NexusWebcamTracker::Config` selects a camera (`device_id`) and describes your
screen (`screen_width_mm`, `screen_height_mm`); set those to your display for
the best accuracy.

**Shipping your application:** the gaze engine lives in
`vendor/webcam-sidecar/`. Install it alongside your application and point
`Config::sidecar_dir` at it:

```cmake
install(DIRECTORY "${HarmonEyesNative_WEBCAM_SIDECAR_DIR}/" DESTINATION bin/webcam-sidecar)
```

> You can supply your own gaze source instead: implement `het::EyeTracker`
> (return the samples gathered since the last call from `get_buffered_data()`)
> and pass it to `set_tracker()`. A default-constructed `het::GazeSample` marks
> every measurement absent, so fill in only what your tracker provides.

See [`examples/theia-webcam-streaming.cpp`](examples/theia-webcam-streaming.cpp).

## Coming from the Python SDK

The method names are the same. The differences are the ones C++ makes natural:

| Python | C++ |
|---|---|
| `TheiaSDK(lease_token=..., platform="PL")` | `TheiaSDK{Device::PupilLabsNeon}` (lease from `THEIA_LEASE`), or `TheiaSDK{options}` |
| `get_cog_load_levels()` → `(dict, batch, lookahead)` | `get_cog_load()` → `std::optional<CogLoadResult>` |
| `get_fatigue_level()` → `(dict, batch)` | `get_fatigue_level()` → `std::optional<FatigueResult>` |
| `get_attention_style()` / `get_mental_readiness(t)` → `dict` or `None` | same names → `std::optional<…>` |
| `sdk.ip = ...; sdk.port = ...` | `sdk.set_ganzin_connection(ip, port)` |
| `sdk.tracker.set_tracker(NexusWebcamTracker())` | `sdk.set_tracker(webcam)` |
| `except LeaseExpiredError` | `catch (const het::LeaseExpiredError&)` |

**Recorded data is Python-only for now.** `predict_cog_load_batch()`,
`predict_fatigue_batch()` and `process_tobii_g3_data()` (Tobii Pro Glasses 3)
are not yet available in C++. Calls the Python SDK keeps only for older code,
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

Alert rules are a JSON document, not a bare array; anything else is rejected:

```json
{ "format": "theia-alert-rules", "version": 1, "etag": "\"v1\"", "rules": [] }
```

Alert rules are configured with `set_alert_rules()`, delivered to
`set_alert_callback()`, and can be kept in sync with your HarmonEyes dashboard
with `enable_alert_sync()`.

## Environment variables

All optional; each has an equivalent in code.

| Variable | Used for | In code |
|---|---|---|
| `THEIA_LEASE` (or `THEIA_LICENSE_KEY`) | your device lease | `Options::lease_token` |
| `THEIA_MODEL_CACHE_DIR` | where downloaded models are cached | `Options::model_cache_dir` |
| `THEIA_NODE` | the Node executable, for the webcam | `NexusWebcamTracker::Config::node_path` |
| `THEIA_WEBCAM_SIDECAR_DIR` | the webcam engine's directory | `NexusWebcamTracker::Config::sidecar_dir` |
| `TOBII_LICENSE_URL` | where the webcam fetches its gaze-engine licence | `NexusWebcamTracker::Config::license_url` |
| `TOBII_LICENSE_API_KEY` | key for that endpoint, sent as `X-License-Key` | `NexusWebcamTracker::Config::license_api_key` |

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
