# HarmonEyes Theia SDK for C++

Real-time cognitive load, fatigue, attention and mental readiness from eye
tracking, for C++ applications.

> This repository distributes **compiled binaries** of the HarmonEyes Theia SDK
> plus the C++ headers that call them. No source ships here.

You need a C++17 compiler, CMake 3.16+, [Git LFS](https://git-lfs.com), a
supported platform (Linux x86_64 or arm64, macOS on Apple silicon, or Windows
x86_64; see [Requirements](#requirements)) and your HarmonEyes API key.

## Get started

### 1. Add the SDK to your build

Clone a release. Each release is a tag; `main` moves between them.

```bash
git lfs install            # once per machine; the binaries are stored with Git LFS
git clone --branch v2.2.2 https://github.com/harmoneyes/theia-cpp.git
```

There is nothing to build or install. Point CMake at the clone:

```cmake
find_package(HarmonEyesNative 2.2 REQUIRED PATHS /path/to/theia-cpp)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE HarmonEyesNative::theia)
harmoneyes_native_copy_runtime(my_app)   # puts the library next to your executable
```

The CMake package keeps this repository's earlier name, `HarmonEyesNative`.
Its version is optional, and accepts that release or any later one with the
same major version.

### 2. Set your API key

```bash
export THEIA_API_KEY_ID="<your API key ID>"
export THEIA_API_KEY_SECRET="<your API key secret>"
```

On first start the SDK gets the machine a device lease from HarmonEyes, then
keeps it current by itself. The machine works offline until its lease expires.
More in [Device Lease](#device-lease).

### 3. Read results

```cpp
#include <harmoneyes/theia.hpp>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace het = harmoneyes::theia;

int main()
{
    het::TheiaSDK sdk{het::Device::PupilLabsNeon};
    sdk.start_new_session("my-first-session");
    sdk.start_realtime_data();

    // Cognitive load and fatigue are handed over once each, so keep the newest.
    std::string cog_load = "warming up";
    std::string fatigue  = "not yet";
    for (int second = 0; second < 60; ++second) {
        std::this_thread::sleep_for(std::chrono::seconds{1});
        if (auto result = sdk.get_cog_load())      { cog_load = result->label; }
        if (auto result = sdk.get_fatigue_level()) { fatigue = result->label; }
        const auto attention = sdk.get_attention_style();
        const auto readiness = sdk.get_mental_readiness();

        std::cout << "Cognitive load: " << cog_load << " | Fatigue: " << fatigue
                  << " | Attention: " << (attention ? attention->label : "not yet")
                  << " | Mental readiness: "
                  << (!readiness ? "not yet"
                      : readiness->low_percentage + readiness->moderate_percentage >= 50.0 ? "High" : "Low")
                  << '\n';
    }
    sdk.stop_processing();
}
```

This reads a Pupil Labs Neon, which the SDK finds on your network. For another
tracker, see [Your eye tracker](#your-eye-tracker). The same program is
[`examples/quickstart.cpp`](examples/quickstart.cpp), and the whole API, with a
line of documentation per call, is in
[`harmoneyes/theia.hpp`](vendor/include/harmoneyes/theia.hpp).

Failures throw: catch `het::LeaseVerificationError` for lease problems, or
`std::exception` for everything. Use a `TheiaSDK` from one thread at a time
(`add_gaze_samples()` is the exception, below). Destroying it stops everything.

## Results

| Call | Returns | Updates |
|---|---|---|
| `get_cog_load()` | `level` (0=Low, 1=Moderate, 2=High), `label`, `confidence` | about once a second, after a ~30 s warmup |
| `get_fatigue_level()` | `level` (0=Alert, 1=Somewhat Fatigued, 2=Fatigued), `label`, `confidence`, time-to-transition | about every 2 minutes |
| `get_attention_style()` | `level` (1=Narrow … 5=Broad), `label` | continuously |
| `get_mental_readiness(elapsed_seconds)` | `low_percentage`, `moderate_percentage`, `high_percentage` of the session's cognitive load. Readiness is **High** while `low_percentage + moderate_percentage` is at least 50, **Low** otherwise. | continuously |

Each returns an empty `std::optional` until a result is available.
`get_cog_load()` and `get_fatigue_level()` return the newest result and clear
it, so poll them about once a second and keep the last one, as the quick start
does. To receive every result instead, set `Options::on_cog_load` and
`Options::on_fatigue`; those callbacks run on the SDK's own threads.

## Your eye tracker

| Device | Tracker | How gaze reaches the SDK |
|---|---|---|
| `Device::PupilLabsNeon` | Pupil Labs Neon | found on your network automatically |
| `Device::Ganzin` | Ganzin Sol | over the network, at the address you give it |
| `Device::Screen` | a screen-based tracker, about 30 samples a second | you push its samples |
| `Device::Headset` | a head-mounted tracker that reports where each eye points | you push its samples |
| `Device::Stasis` | the Stasis head-mounted eye camera | you push its samples |

### Pupil Labs Neon and Ganzin Sol

The SDK connects to these itself. Start them as in the quick start; a Ganzin
Sol also needs its address, and must be powered on and reachable there:

```cpp
het::TheiaSDK sdk{het::Device::Ganzin};
sdk.set_ganzin_connection("192.168.1.100", 8080);   // its IP and port (default 8080), before start_realtime_data()
```

Examples: [`theia-pupil-labs-streaming.cpp`](examples/theia-pupil-labs-streaming.cpp),
[`theia-ganzin-streaming.cpp`](examples/theia-ganzin-streaming.cpp).

### Trackers you feed yourself

For `Device::Screen`, `Device::Headset` and `Device::Stasis`, push each sample
as your tracker delivers it, with `add_gaze_samples()`. There is no
`start_realtime_data()` call:

```cpp
het::TheiaSDK sdk{het::Device::Headset};
sdk.start_new_session(session_id);

// in your tracker's frame callback:
sdk.add_gaze_samples({sample});   // or a std::vector of several, in time order

// when the recording ends:
sdk.stop_processing();
```

- **Threads:** `add_gaze_samples()` may run on another thread, such as that
  callback, while you call the `get_*` methods, but not alongside any other
  call, itself included. About once a second it takes a few milliseconds to
  score the newest data, so keep it off render and UI threads.
- **Unset fields:** a default-constructed `het::GazeSample` marks the optional
  measurements absent, but eye positions default to `0.0`, eye validity to `1`
  (valid) and eye status to `2` (open). On a sample where your tracker lost
  the eye, set those yourself, or a dropped frame goes in as a good one.
- **Connection checks:** `is_connected()` and `has_connection_timeout()` cover
  only trackers the SDK reads itself, not samples you push.

If you'd rather the SDK polled your tracker, see
[Letting the SDK poll your tracker](#letting-the-sdk-poll-your-tracker).

#### Screen-based: `Device::Screen`

Set `gaze_x_norm` and `gaze_y_norm` on every sample: the angular features are
computed from those, not from pixels. Results from trackers that differ much
from about 30 samples a second have not been validated, so contact
support@harmoneyes.com before relying on them.

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

#### Stasis eye camera: `Device::Stasis`

`Device::Stasis` reads these from each `het::GazeSample`:

| Field | Set it to |
|---|---|
| `timestamp_ns` | the capture time, in nanoseconds since the Unix epoch |
| `optical_axis_left_x/y/z` (`_right_` for a right-eye camera) | the camera's gaze vector as it reports it: +x right, +y **down**, +z forward. Leave it at `(0, 0, 0)` when there is no gaze. |
| `left_eye_status` (`right_eye_status` for a right-eye camera) | `2` when the camera found the pupil, `0` when it didn't |

- **Blinks** are read from the frames where the pupil was lost. Leave the
  status alone and they come from gaps in gaze instead, which also counts
  tracking dropouts as blinks.
- **Pupil diameter** is optional: `pupil_diameter_left` in millimetres, if your
  camera measures it.
- **The settings are provisional,** borrowed from head-mounted Tobii glasses,
  and results have not been validated on Stasis recordings yet.

## Reference

### Requirements

- **A C++17 compiler** and **CMake 3.16+**.
- **[Git LFS](https://git-lfs.com)**, to fetch the binaries.
- **Platforms:** Linux x86_64 and aarch64 (arm64) with glibc 2.35 or later,
  macOS 14 or later on Apple silicon, and Windows x86_64. CMake reports
  clearly if your platform is not in a release.
- **On Linux,** the system's libcurl 4, OpenSSL 3 and CA certificates.
  Ubuntu 22.04+ and Debian 12+ ship them; minimal and container images may
  not, so install them with `apt install libcurl4 libssl3 ca-certificates`.
  Without the certificates, the SDK cannot reach HarmonEyes.
- **On Windows,** the Microsoft Visual C++ Redistributable (2015-2022, x64).
  Most machines already have it; otherwise it is a free download from
  Microsoft.
- **Your HarmonEyes API key,** which gives each machine its device lease; see
  [Device Lease](#device-lease).
- **A correct clock.** Leases are checked against the machine's clock, so keep
  it set by NTP. A clock that runs ahead makes a valid lease look expired, and
  the SDK will not start; one that runs behind (a board that boots without a
  battery-backed clock, say) holds off renewal until it is corrected.

### Device Lease

The SDK authenticates with a **device lease**: a signed token for one machine,
verified offline, so a machine keeps working without a connection until its
lease expires. How long that is, your **offline window** (a number of days),
is set on your HarmonEyes account. Besides HarmonEyes, the SDK reaches only the
model CDN (see [Environment variables](#environment-variables) to allowlist or
redirect either).

With your API key set, as in [Get started](#2-set-your-api-key) or through the
`Options` fields `lease_api_key_id` and `lease_api_key_secret`:

- **On first start** the SDK obtains a lease for the machine from HarmonEyes
  (HTTPS to `identity.harmoneyes.io`) and saves it in `.theia/lease` in your
  home directory (`$HOME`, or `%USERPROFILE%` on Windows), readable by your
  user only. To keep it elsewhere, set `THEIA_LEASE_FILE` or
  `Options::lease_file`.
- **From then on** it renews the lease in the background before it is due,
  retrying every 30 minutes while HarmonEyes cannot be reached. The machine
  runs offline until its lease expires.
- **A machine offline for longer than its window** gets a new lease the next
  time it starts online.
- **Each machine registers as** `het::machine_device_identifier()`, a hash of
  its OS machine ID. Machines cloned from one disk image can share that ID,
  and containers usually have none (an empty `/etc/machine-id`); give each its
  own `Options::lease_device_identifier` (a serial number, say). In a
  container, also keep the lease on a volume, or each new container starts
  without one.
- `sdk.lease_info()` reports when the lease expires and how the last renewal
  went.

Your API key can obtain leases for any number of machines on your account, so
keep it as you would a password. HarmonEyes can list your machines and remove
one; a removed machine, or one whose API key was revoked, stops renewing and
does not get a new lease by itself.

#### Without the API key on each machine

Give each machine its first lease and a lease file (`THEIA_LEASE_FILE`), and
put the lease in that file. The SDK renews it from then on; only a machine
that stays offline for longer than its window needs a new first lease. Get
first leases from support@harmoneyes.com, or with your API key, from an admin
machine:

```bash
token=$(curl -s -u "$THEIA_API_KEY_ID:$THEIA_API_KEY_SECRET" https://identity.harmoneyes.io/oidc/token \
  -d grant_type=client_credentials -d scope=account -d resource=https://identity.harmoneyes.io/api |
  sed -n 's/.*"access_token":"\([^"]*\)".*/\1/p')
curl -s https://identity.harmoneyes.io/api/leases -H "Authorization: Bearer $token" \
  -H 'Content-Type: application/json' -d '{"deviceIdentifier":"booth-2"}'
# {"lease":"..."}: one lease per machine, each with its own deviceIdentifier
```

A lease can also be passed directly, in `THEIA_LEASE` (or `THEIA_LICENSE_KEY`)
or `Options::lease_token`. With a lease file set too, the SDK starts from
whichever is newer; without one, nothing keeps a renewed lease across
restarts.

#### Lease errors

An invalid, expired, or missing lease throws `LeaseVerificationError` (or a
more specific subclass such as `LeaseExpiredError`) when constructing
`TheiaSDK`, before any device or model work happens. `LeaseRenewalError`, also
a `LeaseVerificationError`, means HarmonEyes refused a lease or could not be
reached; its message says which. A running process can swap in a lease
obtained elsewhere with `sdk.set_lease_token(lease)`.

### Letting the SDK poll your tracker

Instead of pushing samples, implement `het::EyeTracker`, returning the samples
gathered since the last call from `get_buffered_data()`, and hand it to
`set_tracker()` before starting the stream:

```cpp
MyTracker     tracker;        // implements het::EyeTracker; declare it FIRST
het::TheiaSDK sdk{het::Device::Headset};
sdk.set_tracker(tracker);
sdk.start_new_session(session_id);
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

### Shipping your application

`harmoneyes_native_copy_runtime()` sets up your build tree. To ship, install
the library beside your executable; the same call has already pointed the
executable there (an `$ORIGIN` or `@loader_path` rpath on Linux and macOS):

```cmake
install(TARGETS my_app RUNTIME DESTINATION bin)
install(FILES $<TARGET_FILE:HarmonEyesNative::theia> DESTINATION bin)
```

- **Linux:** the target machine needs libcurl 4, OpenSSL 3 and CA
  certificates (see [Requirements](#requirements)).
- **Services:** an app running as a service (systemd, a Windows service) may
  have no home directory, or none it can write, and the SDK keeps its lease
  and models there. Set `THEIA_LEASE_FILE` and `THEIA_MODEL_CACHE_DIR` to
  paths the service can write, and give it the API key the same way as an
  app.
- **macOS:** `libtheia.dylib` is signed ad hoc. When you sign and notarize
  your app, sign the dylib with your Developer ID along with it, or
  Gatekeeper rejects the app.
- **Windows:** ship `theia.dll` beside your `.exe`; `theia.lib` is only for
  linking. The target machine needs the Visual C++ Redistributable
  (2015-2022, x64), which you can bundle Microsoft's installer for.

### Environment variables

All optional. Those marked — can only be set through the environment.

| Variable | Used for | In code |
|---|---|---|
| `THEIA_API_KEY_ID`, `THEIA_API_KEY_SECRET` | your API key, for obtaining leases | `Options::lease_api_key_id`, `lease_api_key_secret` |
| `THEIA_LEASE_FILE` | where the machine keeps its lease (with an API key, `.theia/lease` in your home directory unless set) | `Options::lease_file` |
| `THEIA_LEASE` (or `THEIA_LICENSE_KEY`) | your device lease | `Options::lease_token` |
| `THEIA_MODEL_CACHE_DIR` | where downloaded models are cached | `Options::model_cache_dir` |
| `THEIA_MODEL_CDN_URL` | where models are downloaded from | — |
| `THEIA_DEVICE_API_URL` | the HarmonEyes API used by `enable_alert_sync()` | its `api_base_url` |
| `THEIA_ALERT_CACHE_DIR` | where synced alert rules are cached | — |

### Tuning and personalisation

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
with `enable_alert_sync()`. `set_alert_rules()` takes a JSON document, not a
bare array; anything else is rejected:

```json
{ "format": "theia-alert-rules", "version": 1, "etag": "\"v1\"", "rules": [] }
```

### Coming from the Python SDK

The method names are the same. The differences are the ones C++ makes natural:

| Python | C++ |
|---|---|
| `TheiaSDK(lease_token=..., platform="PL")` | `TheiaSDK{Device::PupilLabsNeon}` (lease: see [Device Lease](#device-lease)), or `TheiaSDK{options}` |
| `get_cog_load_levels()` → `(dict, batch, lookahead)` | `get_cog_load()` → `std::optional<CogLoadResult>` |
| `get_fatigue_level()` → `(dict, batch)` | `get_fatigue_level()` → `std::optional<FatigueResult>` |
| `get_attention_style()` / `get_mental_readiness(t)` → `dict` or `None` | same names → `std::optional<…>` |
| `sdk.ip = ...; sdk.port = ...` | `sdk.set_ganzin_connection(ip, port)` |
| `sdk.tracker.set_tracker(...)` | `sdk.set_tracker(tracker)` |
| `except LeaseExpiredError` | `catch (const het::LeaseExpiredError&)` |

**Some features are Python-only for now:** the webcam
(`NexusWebcamTracker`), and recorded data: `predict_cog_load_batch()`,
`predict_fatigue_batch()` and `process_tobii_g3_data()` (Tobii Pro Glasses
3). Calls the Python SDK keeps only for older code, such as
`set_window_size()` and `set_profile()`, are left out.

## License & Usage

This software is proprietary and requires a valid device lease to operate (see
[Device Lease](#device-lease) above). The compiled binaries are publicly
distributed but will not function without one.

- **Copyright**: © RightEye LLC / HarmonEyes
- **License Type**: Proprietary / Commercial; see [LICENSE](LICENSE)
- **Obtaining an API key**: contact sales@harmoneyes.com

## Support

For issues, questions, or feature requests, please contact support@harmoneyes.com
or create an issue on GitHub.
