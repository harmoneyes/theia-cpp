# Migration notes

## Coming from the raw C API

If you were calling `theia_export.h` directly, the headers here wrap the same
library, and you can mix the two freely: `theia.hpp` does not hide the C
declarations.

Four things the wrapper does that hand-written C does not.

**Return codes are translated for you.** The C API uses three different
conventions, and getting one wrong is silent. Mutators return `0` on success and `-1` on failure; optional
getters return `0`, `1` for "nothing yet", and `-1`; predicates return `1`, `0`
and `-1`. Since `-1` is truthy, `if (theia_sdk_is_running(h))` reports **true
when the call failed**. The wrapper compares against `1` explicitly.

**Errors become typed exceptions.** Only `theia_sdk_create` and
`theia_sdk_set_lease_token` record a code and message, and they clear it only
on success. Reading the error channel after an unrelated failure therefore
reports whatever happened last, which produces the wrong exception with a
convincing message. The wrapper reads it only where it is valid.

**Borrowed strings are copied at the boundary.** Every `const char*` the C API
returns points into storage the next call overwrites.

**`make_gaze_sample()` applies the real defaults.** A zeroed `gaze_sample_t`
has `blink_id = 0`, which means "part of blink 0", so the pipeline discards
every sample. It also reads as both eyes invalid, lost and zero confidence.

## Coming from the Python SDK

The public names match. The differences are the ones C++ forces.

| Python | C++ |
|---|---|
| `TheiaSDK(...)` | `TheiaSDK{Options{...}}` |
| `sdk.start_new_session(uuid)` | `auto session = sdk.start_session(uuid)` |
| `sdk.stop_processing()` | let the `Session` go out of scope, or `session.stop()` |
| `sdk.get_cog_load()` | `sdk.cog_load()`, returning `std::optional` |
| `except LeaseExpiredError` | `catch (const LeaseExpiredError&)` |
| `sdk.set_tracker(t)` | `TrackerPump pump{sdk, tracker}; pump.start();` |
| `sdk.ip = ...; sdk.port = ...` | `sdk.set_ganzin_connection(ip, port)` |
| `sdk.get_attention_style()` | `sdk.attention()` |
| `sdk.get_mental_fatigue(elapsed_seconds=)` | `sdk.mental_readiness(elapsed_seconds)` |
| `sdk.start_realtime_video()` / `get_video_frame()` | `sdk.start_video()` / `sdk.video_frame()` |
| `sdk.set_mental_fatigue_min_session_seconds(s)` | `sdk.set_mental_readiness_min_session_seconds(s)` |
| `sdk.get_cog_load_prediction_history()` | `sdk.cog_load_history()` |
| `sdk.get_model_info()` | `sdk.model_info()` |
| `sdk.batch_number` / `sdk.batch_size` | `sdk.batch_count()` / `sdk.batch_size()` |
| `sdk.process_tobii_g3_data(df)` | no equivalent; replay the recording instead |
| `NexusWebcamTracker()` | `nexus::WebcamSource` |
| `TobiiLicenseManager()` | `nexus::TobiiLicense::from_env()` |

**Sessions and pumps are objects, not calls.** Python starts a background feed
thread inside `start_realtime_data()`, where its lifetime is invisible. Here a
`TrackerPump` is something you construct and destroy, so the thread ends at a
scope you chose.

**Nothing returns an empty value to mean failure.** Python's getters return
`None` for both "nothing yet" and "it went wrong". Here "nothing yet" is an
empty `std::optional` and a failure is an exception. The one exception is
`version()`, which cannot report a failure because the underlying call records
nothing.

**Stopping a session also stops data streaming**, because that is what the
native `stop_session` does. If you started the tracker with
`start_data_streaming()`, letting a `Session` go out of scope tears the tracker
thread down too. Python has the same behaviour; it is just easier to trip over
when the session is scope-bound.

**`TobiiLicenseManager` does not fetch over HTTP.** Python resolves a URL and
downloads the licence. A header-only C++ SDK should not oblige every consumer
to link an HTTP stack, so `TobiiLicense` takes the bytes and handles the
UTF-16LE decoding; fetching them is yours.

**There is no batch scoring call.** Python's `predict_cog_load_batch()`,
`predict_fatigue_batch()` and `process_tobii_g3_data()` parse a vendor export
into features using a second compiled extension, which this SDK does not ship.
The feature extractor, not the scoring, is what is missing. Scoring a recording
from C++ means reading it into `GazeSample` values, feeding them in and
collecting the callbacks, which `examples/theia-pupil-labs-batch.cpp` does end
to end.

**`attention()` and `mental_readiness()` do not clear.** Python's getters and
these both return rolling state rather than an event, so polling them twice
returns the same value until a new window lands. `cog_load()` and `fatigue()`
do clear, in both SDKs.

**Tobii G3 streams live here.** The Python example scores a recorded Pro Lab
export because that is what its helper does; the library itself connects to the
glasses, discovering them over mDNS. Set the gaze rate and calibrate before the
session starts, and read the note on `set_tobii_g3_gaze_frequency()` before
choosing 100 Hz.

**Two names are not ported.** `DirectUploader` is an empty stub in Python with
no methods, and `DefaultTrackerVideo` needs OpenCV.
