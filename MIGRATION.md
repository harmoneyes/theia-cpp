# Migration notes

## Coming from the raw C API

If you were calling `theia_export.h` directly, the headers here wrap the same
library, and you can mix the two freely: `theia.hpp` does not hide the C
declarations.

Four things the wrapper does that hand-written C does not.

**Return codes are translated for you.** The C API uses three different
conventions, and `theia-native/docs/c-api.md` currently documents them
incorrectly. Mutators return `0` on success and `-1` on failure; optional
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

**Two names are not ported.** `DirectUploader` is an empty stub in Python with
no methods, and `DefaultTrackerVideo` needs OpenCV.
