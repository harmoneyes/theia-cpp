#pragma once

// Definitions for <harmoneyes/theia.hpp>. Not part of the API, and not meant
// to be included on its own.

#ifndef HARMONEYES_THEIA_INCLUDING_IMPL
#  error "Include <harmoneyes/theia.hpp> instead of this file."
#endif

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <map>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

#include <harmoneyes/theia_export.h>

namespace harmoneyes::theia {

inline std::string version()
{
    const char* v = ::theia_sdk_version();
    return v ? std::string{v} : std::string{};
}

inline std::string machine_device_identifier()
{
    const char* id = ::theia_machine_device_identifier();
    return id ? std::string{id} : std::string{};
}

inline const char* to_string(CogLoadLevel level) noexcept
{
    switch (level) {
        case CogLoadLevel::Low:      return "Low";
        case CogLoadLevel::Moderate: return "Moderate";
        case CogLoadLevel::High:     return "High";
    }
    return "Unknown";
}

inline const char* to_string(FatigueLevel level) noexcept
{
    switch (level) {
        case FatigueLevel::Alert:            return "Alert";
        case FatigueLevel::SomewhatFatigued: return "Somewhat Fatigued";
        case FatigueLevel::Fatigued:         return "Fatigued";
    }
    return "Unknown";
}

// GazeSample is passed to the library as a gaze_sample_t, so the two must
// have identical layouts. Checked field by field, so a change on either side
// fails the build instead of silently shifting what every field means.
#define HARMONEYES_THEIA_SAME_FIELD(f)                                        \
    static_assert(offsetof(GazeSample, f) == offsetof(::gaze_sample_t, f) && \
                      sizeof(GazeSample::f) == sizeof(::gaze_sample_t::f),     \
                  "GazeSample::" #f " does not match gaze_sample_t")
static_assert(std::is_standard_layout<GazeSample>::value, "GazeSample must stay standard-layout");
static_assert(sizeof(GazeSample) == sizeof(::gaze_sample_t) &&
                  alignof(GazeSample) == alignof(::gaze_sample_t),
              "GazeSample does not match gaze_sample_t");
HARMONEYES_THEIA_SAME_FIELD(timestamp_ns);
HARMONEYES_THEIA_SAME_FIELD(left_eye_x);
HARMONEYES_THEIA_SAME_FIELD(left_eye_y);
HARMONEYES_THEIA_SAME_FIELD(right_eye_x);
HARMONEYES_THEIA_SAME_FIELD(right_eye_y);
HARMONEYES_THEIA_SAME_FIELD(pupil_diameter_left);
HARMONEYES_THEIA_SAME_FIELD(pupil_diameter_right);
HARMONEYES_THEIA_SAME_FIELD(pupil_diameter);
HARMONEYES_THEIA_SAME_FIELD(depth);
HARMONEYES_THEIA_SAME_FIELD(left_eye_validity);
HARMONEYES_THEIA_SAME_FIELD(right_eye_validity);
HARMONEYES_THEIA_SAME_FIELD(left_eye_status);
HARMONEYES_THEIA_SAME_FIELD(right_eye_status);
HARMONEYES_THEIA_SAME_FIELD(left_eye_confidence);
HARMONEYES_THEIA_SAME_FIELD(right_eye_confidence);
HARMONEYES_THEIA_SAME_FIELD(blink_id);
HARMONEYES_THEIA_SAME_FIELD(eyeball_center_left_x);
HARMONEYES_THEIA_SAME_FIELD(eyeball_center_left_y);
HARMONEYES_THEIA_SAME_FIELD(eyeball_center_left_z);
HARMONEYES_THEIA_SAME_FIELD(optical_axis_left_x);
HARMONEYES_THEIA_SAME_FIELD(optical_axis_left_y);
HARMONEYES_THEIA_SAME_FIELD(optical_axis_left_z);
HARMONEYES_THEIA_SAME_FIELD(eyeball_center_right_x);
HARMONEYES_THEIA_SAME_FIELD(eyeball_center_right_y);
HARMONEYES_THEIA_SAME_FIELD(eyeball_center_right_z);
HARMONEYES_THEIA_SAME_FIELD(optical_axis_right_x);
HARMONEYES_THEIA_SAME_FIELD(optical_axis_right_y);
HARMONEYES_THEIA_SAME_FIELD(optical_axis_right_z);
HARMONEYES_THEIA_SAME_FIELD(eyelid_angle_top_left);
HARMONEYES_THEIA_SAME_FIELD(eyelid_angle_bottom_left);
HARMONEYES_THEIA_SAME_FIELD(eyelid_angle_top_right);
HARMONEYES_THEIA_SAME_FIELD(eyelid_angle_bottom_right);
HARMONEYES_THEIA_SAME_FIELD(eyelid_aperture_left_mm);
HARMONEYES_THEIA_SAME_FIELD(eyelid_aperture_right_mm);
HARMONEYES_THEIA_SAME_FIELD(accel_x);
HARMONEYES_THEIA_SAME_FIELD(accel_y);
HARMONEYES_THEIA_SAME_FIELD(accel_z);
HARMONEYES_THEIA_SAME_FIELD(gyro_x);
HARMONEYES_THEIA_SAME_FIELD(gyro_y);
HARMONEYES_THEIA_SAME_FIELD(gyro_z);
HARMONEYES_THEIA_SAME_FIELD(quat_w);
HARMONEYES_THEIA_SAME_FIELD(quat_x);
HARMONEYES_THEIA_SAME_FIELD(quat_y);
HARMONEYES_THEIA_SAME_FIELD(quat_z);
HARMONEYES_THEIA_SAME_FIELD(gaze_x_norm);
HARMONEYES_THEIA_SAME_FIELD(gaze_y_norm);
HARMONEYES_THEIA_SAME_FIELD(screen_width_mm);
HARMONEYES_THEIA_SAME_FIELD(screen_height_mm);
HARMONEYES_THEIA_SAME_FIELD(screen_res_width_px);
HARMONEYES_THEIA_SAME_FIELD(screen_res_height_px);
HARMONEYES_THEIA_SAME_FIELD(head_pose_depth_cm);
HARMONEYES_THEIA_SAME_FIELD(head_pose_tz);
HARMONEYES_THEIA_SAME_FIELD(gaze_yaw_deg);
HARMONEYES_THEIA_SAME_FIELD(gaze_pitch_deg);
HARMONEYES_THEIA_SAME_FIELD(eyes_closed);
#undef HARMONEYES_THEIA_SAME_FIELD

}  // namespace harmoneyes::theia

namespace harmoneyes::theia::detail {

// -- Strings from the library --
//
// Every `const char*` the library returns points into storage it reuses on
// the next call, so each one is copied before it is returned.

[[nodiscard]] inline std::string copy_borrowed(const char* s)
{
    return s ? std::string{s} : std::string{};
}

// -- Return codes --
//
// Only creating the SDK, setting the lease, renewing it and subscribing
// record why they failed; every other call reports failure with -1 alone. So
// the recorded reason is read only after those, never after anything else,
// where it would describe some earlier failure.
//
// Nothing here may be called from a callback trampoline: an exception must
// not unwind through the library's frames.

/// Throw the recorded failure. Only valid straight after a failed
/// theia_sdk_create(), theia_sdk_set_lease_token(), theia_sdk_renew_lease()
/// or theia_sdk_subscribe_*() on this thread.
[[noreturn]] inline void throw_recorded_error(const char* context)
{
    const int   code   = static_cast<int>(::theia_get_last_error_code());
    const char* reason = ::theia_get_last_error_message();
    const std::string message =
        std::string{context} + ": " + ((reason && *reason) ? reason : "no detail reported");

    switch (code) {
        case THEIA_ERROR_INVALID_ARGUMENT:      throw std::invalid_argument{message};
        case THEIA_ERROR_MODEL_UNAVAILABLE:     throw ModelUnavailableError{message};
        case THEIA_ERROR_LEASE_MALFORMED:       throw LeaseMalformedError{message};
        case THEIA_ERROR_LEASE_UNKNOWN_KEY:     throw LeaseUnknownKeyError{message};
        case THEIA_ERROR_LEASE_BAD_SIGNATURE:   throw LeaseSignatureError{message};
        case THEIA_ERROR_LEASE_EXPIRED:         throw LeaseExpiredError{message};
        case THEIA_ERROR_LEASE_DEVICE_MISMATCH: throw LeaseDeviceMismatchError{message};
        case THEIA_ERROR_LEASE_TENANT_MISMATCH: throw LeaseTenantMismatchError{message};
        case THEIA_ERROR_LEASE_RENEWAL:         throw LeaseRenewalError{message};
        default:                                throw Error{message};
    }
}

/// Throw for a call that records no reason.
[[noreturn]] inline void throw_opaque_error(const char* context)
{
    throw Error{std::string{context} +
                " failed (no further detail is available; set Options::on_log to see the "
                "SDK's log)"};
}

/// Constructors: null on failure, with a recorded reason.
[[nodiscard]] inline ::theia_sdk_t* check_handle(::theia_sdk_t* handle, const char* context)
{
    if (handle == nullptr) { throw_recorded_error(context); }
    return handle;
}

/// Commands: 0 on success, -1 on failure.
inline void check(int rc, const char* context)
{
    if (rc != 0) { throw_opaque_error(context); }
}

/// Optional getters: 0 wrote a value, 1 means none yet, -1 failed. Returns
/// whether a value was written.
[[nodiscard]] inline bool check_optional(int rc, const char* context)
{
    if (rc == 0) { return true; }
    if (rc == 1) { return false; }
    throw_opaque_error(context);
}

/// Predicates: 1 true, 0 false, -1 failed. Compared against 1 explicitly,
/// because -1 is truthy.
[[nodiscard]] inline bool check_bool(int rc, const char* context)
{
    if (rc == 1) { return true; }
    if (rc == 0) { return false; }
    throw_opaque_error(context);
}

/// Operations that report success as a predicate rather than as a command:
/// the C entry point returns `self->thing() ? 1 : 0`, so 0 means the library
/// declined and -1 means it threw. Both are failures. Passing these to
/// check() inverts them: success throws and failure passes silently.
inline void check_succeeded(int rc, const char* context)
{
    if (rc != 1) { throw_opaque_error(context); }
}

/// An environment variable's value, or empty when it is unset. The value is
/// copied at once, so MSVC's warning about std::getenv (C4996) does not
/// apply, and is silenced here rather than in every customer's build.
[[nodiscard]] inline std::string getenv_copy(const char* name)
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return value != nullptr ? std::string{value} : std::string{};
}

// -- Conversions --

/// The platform name the library expects for a device.
[[nodiscard]] inline const char* platform_string(Device device) noexcept
{
    switch (device) {
        case Device::Ganzin: return "Ganzin";
        // The library's webcam (Tobii Nexus) profile, tuned for screen-based gaze.
        case Device::Screen: return "Webcam";
        // The library's Tobii Pro Glasses 3 profile, which reads gaze direction
        // and pupil size only. With a tracker supplied, it never connects to
        // glasses itself.
        case Device::Headset: return "TobiiG3";
        // Its own profile: one eye at 60 Hz, gaze +y down, blinks from pupil
        // detection or gaze gaps when there is no pupil size.
        case Device::Stasis: return "Stasis";
        // Its own profile, at 200 Hz.
        case Device::VarjoXR3: return "VarjoXR3";
        // The Pupil Labs Neon profile, tuned for the Quest's 72 Hz.
        case Device::Quest: return "Quest";
        case Device::PupilLabsNeon:
            break;
    }
    return "PL";
}

/// "Name: value" lines joined the way the library takes them.
[[nodiscard]] inline std::string join_lines(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& line : lines) {
        if (!out.empty()) { out += '\n'; }
        out += line;
    }
    return out;
}

[[nodiscard]] inline AlertEvent to_alert_event(const ::alert_event_t& c)
{
    AlertEvent e;
    e.firing_id           = copy_borrowed(c.firing_id);
    e.rule_id             = copy_borrowed(c.rule_id);
    e.rule_name           = copy_borrowed(c.rule_name);
    e.input               = copy_borrowed(c.input);
    e.level               = c.level;
    e.previous_level      = c.previous_level;
    e.trigger             = copy_borrowed(c.trigger);
    e.sustained_sec       = c.sustained_sec;
    e.level_share_percent = c.level_share_percent;
    e.message             = copy_borrowed(c.message);
    e.timestamp_ns        = c.timestamp_ns;
    e.fired_at_unix_ms    = c.fired_at_unix_ms;
    return e;
}

[[nodiscard]] inline AlertRuleInfo to_alert_rule_info(const ::alert_rule_info_t& c)
{
    AlertRuleInfo r;
    r.id                     = copy_borrowed(c.id);
    r.name                   = copy_borrowed(c.name);
    r.summary                = copy_borrowed(c.summary);
    r.input                  = copy_borrowed(c.input);
    r.input_name             = copy_borrowed(c.input_name);
    r.levels                 = copy_borrowed(c.levels);
    r.on_increase            = c.on_increase != 0;
    r.on_decrease            = c.on_decrease != 0;
    r.min_duration_sec       = c.min_duration_sec;
    r.level_share_percent    = c.level_share_percent;
    r.level_share_window_sec = c.level_share_window_sec;
    r.cooldown_sec           = c.cooldown_sec;
    r.readiness_window_sec   = c.readiness_window_sec;
    r.source                 = copy_borrowed(c.source);
    r.last_fired_at_unix_ms  = c.last_fired_at_unix_ms;
    return r;
}

[[nodiscard]] inline AlertRulesStatus to_alert_rules_status(const ::alert_rules_status_t& c)
{
    AlertRulesStatus s;
    s.source                     = copy_borrowed(c.source);
    s.rule_count                 = c.rule_count;
    s.skipped_rule_count         = c.skipped_rule_count;
    s.skipped_detail             = copy_borrowed(c.skipped_detail);
    s.etag                       = copy_borrowed(c.etag);
    s.generated_at               = copy_borrowed(c.generated_at);
    s.tenant_id                  = copy_borrowed(c.tenant_id);
    s.subject                    = copy_borrowed(c.subject);
    s.loaded_at_unix_ms          = c.loaded_at_unix_ms;
    s.last_error                 = copy_borrowed(c.last_error);
    s.sync_state                 = copy_borrowed(c.sync_state);
    s.sync_url                   = copy_borrowed(c.sync_url);
    s.last_sync_attempt_unix_ms  = c.last_sync_attempt_unix_ms;
    s.last_sync_success_unix_ms  = c.last_sync_success_unix_ms;
    s.sync_error                 = copy_borrowed(c.sync_error);
    s.pending_firings            = c.pending_firings;
    s.last_firing_upload_unix_ms = c.last_firing_upload_unix_ms;
    s.firing_upload_error        = copy_borrowed(c.firing_upload_error);
    return s;
}

[[nodiscard]] inline CogLoadResult to_cog_load(const ::cog_load_result_t& c)
{
    CogLoadResult r;
    r.level        = static_cast<CogLoadLevel>(c.level);
    r.label        = to_string(r.level);
    r.confidence   = c.confidence;
    r.batch_number = c.batch_number;
    r.timestamp_ns = c.timestamp_ns;
    r.prob_high    = c.prob_high;
    r.prob_low     = c.prob_low;
    r.level_duration_sec = c.level_duration_sec;
    return r;
}

[[nodiscard]] inline FatigueResult to_fatigue(const ::fatigue_result_t& c)
{
    FatigueResult r;
    r.level        = static_cast<FatigueLevel>(c.level);
    r.label        = to_string(r.level);
    r.confidence   = c.confidence;
    r.batch_number = c.batch_number;
    // The C layer uses -1 for "absent".
    if (c.ttt_level >= 0)   { r.ttt_level   = static_cast<FatigueLevel>(c.ttt_level); }
    if (c.ttt_minutes >= 0) { r.ttt_minutes = c.ttt_minutes; }
    r.level_duration_sec = c.level_duration_sec;
    return r;
}

[[nodiscard]] inline AttentionResult to_attention(const ::attention_result_t& c)
{
    AttentionResult r;
    r.level              = static_cast<AttentionLevel>(c.level);
    r.label              = copy_borrowed(c.label);
    r.composite_score    = c.composite_score;
    r.total_observations = c.total_observations;
    r.level_duration_sec = c.level_duration_sec;
    return r;
}

[[nodiscard]] inline MentalReadinessResult to_mental_readiness(const ::mental_readiness_result_t& c)
{
    MentalReadinessResult r;
    r.low_percentage      = c.low_percentage;
    r.moderate_percentage = c.moderate_percentage;
    r.high_percentage     = c.high_percentage;
    r.total_predictions   = c.total_predictions;
    return r;
}

[[nodiscard]] inline MentalReadinessWindowMeasurement
to_window(const ::mental_readiness_window_measurement_t& c)
{
    MentalReadinessWindowMeasurement w;
    w.nominal_window_seconds       = c.nominal_window_seconds;
    w.window_start_timestamp_ns    = c.window_start_timestamp_ns;
    w.window_end_timestamp_ns      = c.window_end_timestamp_ns;
    w.low_cog_load_percentage      = c.low_cog_load_percentage;
    w.moderate_cog_load_percentage = c.moderate_cog_load_percentage;
    w.high_cog_load_percentage     = c.high_cog_load_percentage;
    w.mental_readiness_percentage  = c.mental_readiness_percentage;
    w.level                        = static_cast<MentalReadinessLevel>(c.level);
    w.predictions_in_window        = c.predictions_in_window;
    w.level_duration_sec           = c.level_duration_sec;
    return w;
}

/// Copies the windows, which the library lends only for the callback.
[[nodiscard]] inline MentalReadinessWindowsResult
to_mental_readiness_windows(const ::mental_readiness_ace_result_t& c)
{
    MentalReadinessWindowsResult r;
    if (c.windows != nullptr) {
        r.windows.reserve(c.windows_count);
        for (std::size_t i = 0; i < c.windows_count; ++i) { r.windows.push_back(to_window(c.windows[i])); }
    }
    r.timestamp_ns = c.timestamp_ns;
    return r;
}

/// The cognitive-load, fatigue and attention level changes, which share a
/// layout.
template <typename Event, typename CEvent>
[[nodiscard]] Event to_transition(const CEvent& c)
{
    using Level = decltype(Event::previous_level);
    Event e;
    e.previous_level              = static_cast<Level>(c.previous_level);
    e.new_level                   = static_cast<Level>(c.new_level);
    e.reason                      = static_cast<TransitionReason>(c.reason);
    e.timestamp_ns                = c.timestamp_ns;
    e.previous_level_duration_sec = c.previous_level_duration_sec;
    return e;
}

[[nodiscard]] inline MentalReadinessTransitionEvent
to_mental_readiness_transition(const ::mental_readiness_transition_event_t& c)
{
    MentalReadinessTransitionEvent e;
    e.nominal_window_seconds       = c.nominal_window_seconds;
    e.actual_window_length_seconds = c.actual_window_length_seconds;
    e.previous_level               = static_cast<MentalReadinessLevel>(c.previous_level);
    e.new_level                    = static_cast<MentalReadinessLevel>(c.new_level);
    e.reason                       = static_cast<TransitionReason>(c.reason);
    e.mental_readiness_percentage  = c.mental_readiness_percentage;
    e.timestamp_ns                 = c.timestamp_ns;
    e.previous_level_duration_sec  = c.previous_level_duration_sec;
    return e;
}

// -- Subscriptions --

/// One subscription's callbacks. The library holds only a pointer to them.
struct SubscriptionCallbacks {
    virtual ~SubscriptionCallbacks() = default;

    SubscriptionCallbacks()                                        = default;
    SubscriptionCallbacks(const SubscriptionCallbacks&)            = delete;
    SubscriptionCallbacks& operator=(const SubscriptionCallbacks&) = delete;
    SubscriptionCallbacks(SubscriptionCallbacks&&)                 = delete;
    SubscriptionCallbacks& operator=(SubscriptionCallbacks&&)      = delete;
};

template <typename Event>
struct SubscriptionCallbacksOf final : SubscriptionCallbacks {
    std::vector<std::function<void(const Event&)>> list;
};

/// How many subscription callbacks this thread is inside. unsubscribe() reads
/// it: the library cannot wait out a callback that is itself unsubscribing,
/// so in there the callbacks must not be freed yet.
[[nodiscard]] inline int& subscription_callback_depth() noexcept
{
    static thread_local int depth = 0;
    return depth;
}

/// The trampoline for every subscription: converts the event once, then
/// calls each callback in order. Runs on the library's threads behind
/// extern "C" frames, so nothing may escape it.
template <typename Event, typename CEvent, Event (*Convert)(const CEvent&)>
void deliver_subscribed(CEvent c, void* user_data) noexcept
{
    auto* callbacks = static_cast<SubscriptionCallbacksOf<Event>*>(user_data);
    if (callbacks == nullptr) { return; }
    ++subscription_callback_depth();
    try {
        const Event event = Convert(c);
        for (const auto& callback : callbacks->list) {
            try { callback(event); } catch (...) {}
        }
    } catch (...) {}
    --subscription_callback_depth();
}

/// Copy a video_frame_t out of the library's reused buffers.
[[nodiscard]] inline VideoFrame to_video_frame(const ::video_frame_t& c)
{
    const auto copy = [](const std::uint8_t* data, std::size_t len) {
        return (data != nullptr && len != 0) ? std::vector<std::uint8_t>{data, data + len}
                                             : std::vector<std::uint8_t>{};
    };

    VideoFrame f;
    f.bgr_data               = copy(c.bgr_data, c.bgr_data_len);
    f.encoded_data           = copy(c.encoded_data, c.encoded_data_len);
    f.extradata              = copy(c.extradata, c.extradata_len);
    f.width                  = c.width;
    f.height                 = c.height;
    f.timestamp_unix_seconds = c.timestamp_unix_seconds;
    f.is_keyframe            = c.is_keyframe == 1;
    return f;
}

[[nodiscard]] inline const ::gaze_sample_t* as_c_samples(const GazeSample* samples) noexcept
{
    // Layout-identical, as checked above.
    return reinterpret_cast<const ::gaze_sample_t*>(samples);
}

[[nodiscard]] inline std::vector<::ground_truth_point_t>
to_c_ground_truth(const std::vector<GroundTruthPoint>& points)
{
    std::vector<::ground_truth_point_t> out;
    out.reserve(points.size());
    for (const auto& p : points) { out.push_back({p.time_unix_seconds, p.value}); }
    return out;
}

}  // namespace harmoneyes::theia::detail

namespace harmoneyes::theia {

/// Everything the library holds a pointer into, plus the tracker feed.
///
/// Allocated once and never moved, so the pointer handed to the library as
/// callback user_data, and the one the feed thread uses, stay valid across a
/// move of the owning TheiaSDK.
struct TheiaSDK::State {
    Options        options;
    /// Shared so that a delivery holds its own reference: replacing the
    /// callback from inside it then cannot destroy the call under way.
    std::shared_ptr<const AlertCallback> alert_callback;
    ::theia_sdk_t* handle = nullptr;

    bool session_open      = false;
    bool running           = false;
    bool native_streaming  = false;
    bool video_started     = false;

    EyeTracker*       tracker = nullptr;
    std::thread       feed;
    std::atomic<bool> feeding{false};
    std::atomic<bool> feed_timed_out{false};

    /// The tracker's is_connected(), as last sampled by the feed thread.
    /// While that thread runs, TheiaSDK::is_connected() reads this rather
    /// than calling the tracker, which would race with get_buffered_data().
    /// When it is not running, nothing races, so the tracker is asked directly
    /// and the answer is never stale.
    std::atomic<bool> tracker_connected{false};

    /// Each live subscription's callbacks, by id. A callback may unsubscribe,
    /// so the map has its own lock.
    std::mutex subscriptions_mutex;
    std::map<std::uint64_t, std::unique_ptr<detail::SubscriptionCallbacks>> subscriptions;
    /// Subscriptions removed from inside a callback, which may still be
    /// running. Freed with the State, after the library has stopped.
    std::vector<std::unique_ptr<detail::SubscriptionCallbacks>> unsubscribed_in_callback;

    static constexpr std::chrono::milliseconds kFeedInterval{10};
    static constexpr std::chrono::seconds      kFeedTimeout{5};

    explicit State(Options o) : options(std::move(o)) {}

    void log(const std::string& message) noexcept
    {
        if (!options.on_log) { return; }
        try { options.on_log(message); } catch (...) {}
    }

    void start_feed()
    {
        feed_timed_out    = false;
        tracker_connected = tracker->is_connected();   // before the thread exists
        feeding           = true;
        try {
            feed = std::thread([this] { feed_loop(); });
        } catch (...) {
            feeding = false;
            throw;
        }
    }

    void stop_feed() noexcept
    {
        feeding = false;
        if (feed.joinable()) { feed.join(); }
    }

    void feed_loop() noexcept
    {
        using clock    = std::chrono::steady_clock;
        auto last_data = clock::now();
        while (feeding.load()) {
            try {
                tracker_connected = tracker->is_connected();
                const std::vector<GazeSample> batch = tracker->get_buffered_data();
                if (!batch.empty()) {
                    // The library copies each sample and never writes through
                    // this pointer, despite its non-const signature.
                    if (::theia_sdk_add_gaze_samples(
                            handle, const_cast<::gaze_sample_t*>(detail::as_c_samples(batch.data())),
                            batch.size()) != 0) {
                        log("TheiaSDK: the pipeline rejected a batch of gaze samples");
                    }
                    last_data      = clock::now();
                    feed_timed_out = false;
                } else if (!feed_timed_out && clock::now() - last_data > kFeedTimeout) {
                    feed_timed_out = true;
                    log("TheiaSDK: no gaze samples from the tracker for 5 seconds");
                }
            } catch (const std::exception& e) {
                log(std::string{"TheiaSDK: tracker error: "} + e.what());
            } catch (...) {
                log("TheiaSDK: tracker error");
            }
            std::this_thread::sleep_for(kFeedInterval);
        }
    }

    /// Tear everything down. Never throws; used by the destructor.
    void shutdown() noexcept
    {
        stop_feed();
        if (running && tracker != nullptr) {
            try { tracker->close(); } catch (...) {}
        }
        running = false;
        if (handle != nullptr) {
            if (session_open) { ::theia_sdk_stop_session(handle); }
            // destroy() joins every thread that can invoke a callback, and
            // those callbacks dereference this State, so it must still exist.
            ::theia_sdk_destroy(handle);
            handle = nullptr;
        }
        session_open = false;
    }

    // Callback trampolines. These run on the library's threads behind
    // extern "C" frames, so nothing may escape them.
    static void on_log(const char* message, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr) { return; }
        self->log(detail::copy_borrowed(message));
    }

    static void on_cog_load(::cog_load_result_t result, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_cog_load) { return; }
        try { self->options.on_cog_load(detail::to_cog_load(result)); } catch (...) {}
    }

    static void on_fatigue(::fatigue_result_t result, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_fatigue) { return; }
        try { self->options.on_fatigue(detail::to_fatigue(result)); } catch (...) {}
    }

    static void on_attention(::attention_result_t result, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_attention) { return; }
        try { self->options.on_attention(detail::to_attention(result)); } catch (...) {}
    }

    static void on_mental_readiness(::mental_readiness_ace_result_t result, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_mental_readiness) { return; }
        try { self->options.on_mental_readiness(detail::to_mental_readiness_windows(result)); } catch (...) {}
    }

    static void on_cog_load_transition(::cog_load_transition_event_t event, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_cog_load_transition) { return; }
        try {
            self->options.on_cog_load_transition(detail::to_transition<CogLoadTransitionEvent>(event));
        } catch (...) {}
    }

    static void on_fatigue_transition(::fatigue_transition_event_t event, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_fatigue_transition) { return; }
        try {
            self->options.on_fatigue_transition(detail::to_transition<FatigueTransitionEvent>(event));
        } catch (...) {}
    }

    static void on_attention_transition(::attention_transition_event_t event, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_attention_transition) { return; }
        try {
            self->options.on_attention_transition(detail::to_transition<AttentionTransitionEvent>(event));
        } catch (...) {}
    }

    static void on_mental_readiness_transition(::mental_readiness_transition_event_t event,
                                               void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_mental_readiness_transition) { return; }
        try {
            self->options.on_mental_readiness_transition(detail::to_mental_readiness_transition(event));
        } catch (...) {}
    }

    /// Everything the subscribe_* methods share. `Convert` turns the
    /// library's event into the C++ one; `c_subscribe` is the library's
    /// subscribe for that event.
    template <typename Event, typename CEvent, Event (*Convert)(const CEvent&), typename Handler>
    std::uint64_t subscribe(std::uint64_t (*c_subscribe)(::theia_sdk_t*, const Handler*, std::size_t,
                                                         ::subscription_options_t),
                            std::vector<std::function<void(const Event&)>> callbacks,
                            const SubscriptionOptions& settings, const char* context)
    {
        auto record = std::make_unique<detail::SubscriptionCallbacksOf<Event>>();
        for (auto& callback : callbacks) {
            if (callback) { record->list.push_back(std::move(callback)); }
        }

        const std::string headers = detail::join_lines(settings.webhook_headers);
        ::subscription_options_t c{};
        if (!settings.filter.empty())      { c.filter = settings.filter.c_str(); }
        if (!settings.webhook_url.empty()) { c.webhook_url = settings.webhook_url.c_str(); }
        if (!headers.empty())              { c.webhook_headers = headers.c_str(); }

        // With no callbacks (a webhook alone), the library gets no handler.
        const Handler     handler{&detail::deliver_subscribed<Event, CEvent, Convert>, record.get()};
        const std::size_t count = record->list.empty() ? 0U : 1U;
        const std::uint64_t id  = c_subscribe(handle, count != 0U ? &handler : nullptr, count, c);
        if (id == 0) { detail::throw_recorded_error(context); }

        const std::lock_guard<std::mutex> lock(subscriptions_mutex);
        subscriptions.emplace(id, std::move(record));
        return id;
    }

    static void on_lease_renewed(const char* lease_token, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_lease_renewed) { return; }
        try { self->options.on_lease_renewed(detail::copy_borrowed(lease_token)); } catch (...) {}
    }

    static void on_alert(::alert_event_t event, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr) { return; }
        const std::shared_ptr<const AlertCallback> callback = self->alert_callback;
        if (!callback) { return; }
        try { (*callback)(detail::to_alert_event(event)); } catch (...) {}
    }
};

inline TheiaSDK::TheiaSDK(Device device)
    : TheiaSDK([device] {
          Options o;
          o.device = device;
          return o;
      }())
{
}

inline TheiaSDK::TheiaSDK(Options options)
    : state_(std::make_shared<State>(std::move(options)))
{
    Options& o = state_->options;

    if (o.lease_token.empty()) { o.lease_token = detail::getenv_copy("THEIA_LEASE"); }
    if (o.lease_token.empty()) { o.lease_token = detail::getenv_copy("THEIA_LICENSE_KEY"); }

    ::options_t c{};
    c.platform = detail::platform_string(o.device);

    // The C layer takes these inverted, so that a zeroed options_t keeps both
    // models on.
    c.disable_cog_load_model = o.enable_cog_load_model ? 0 : 1;
    c.disable_fatigue_model  = o.enable_fatigue_model ? 0 : 1;

    if (!o.lease_token.empty())              { c.lease_token = o.lease_token.c_str(); }
    if (!o.lease_expected_device_id.empty()) { c.lease_expected_device_id = o.lease_expected_device_id.c_str(); }
    if (!o.lease_expected_tenant_id.empty()) { c.lease_expected_tenant_id = o.lease_expected_tenant_id.c_str(); }
    if (!o.model_cache_dir.empty())          { c.model_cache_dir = o.model_cache_dir.c_str(); }

    // Left empty, the library reads $THEIA_LEASE_FILE, $THEIA_API_KEY_ID and
    // $THEIA_API_KEY_SECRET itself.
    if (!o.lease_file.empty())              { c.lease_file = o.lease_file.c_str(); }
    if (!o.lease_api_key_id.empty())        { c.lease_api_key_id = o.lease_api_key_id.c_str(); }
    if (!o.lease_api_key_secret.empty())    { c.lease_api_key_secret = o.lease_api_key_secret.c_str(); }
    if (!o.lease_device_identifier.empty()) { c.lease_device_identifier = o.lease_device_identifier.c_str(); }
    c.disable_lease_auto_renew = o.disable_lease_auto_renew ? 1 : 0;

    // Left empty, the control server runs with its defaults.
    if (!o.rpc_config_path.empty()) { c.rpc_config_path = o.rpc_config_path.c_str(); }
    c.disable_rpc_server = o.disable_rpc_server ? 1 : 0;

    if (o.on_log)      { c.log_callback = &State::on_log;               c.log_callback_user_data = state_.get(); }
    if (o.on_cog_load) { c.cog_load_ace_callback = &State::on_cog_load; c.cog_load_ace_callback_user_data = state_.get(); }
    if (o.on_fatigue)  { c.fatigue_callback = &State::on_fatigue;       c.fatigue_callback_user_data = state_.get(); }
    if (o.on_attention) {
        c.attention_ace_callback           = &State::on_attention;
        c.attention_ace_callback_user_data = state_.get();
    }
    if (o.on_mental_readiness) {
        c.mental_readiness_ace_callback           = &State::on_mental_readiness;
        c.mental_readiness_ace_callback_user_data = state_.get();
    }
    if (o.on_cog_load_transition) {
        c.cog_load_transition_ace_callback           = &State::on_cog_load_transition;
        c.cog_load_transition_ace_callback_user_data = state_.get();
    }
    if (o.on_fatigue_transition) {
        c.fatigue_transition_ace_callback           = &State::on_fatigue_transition;
        c.fatigue_transition_ace_callback_user_data = state_.get();
    }
    if (o.on_attention_transition) {
        c.attention_transition_ace_callback           = &State::on_attention_transition;
        c.attention_transition_ace_callback_user_data = state_.get();
    }
    if (o.on_mental_readiness_transition) {
        c.mental_readiness_transition_ace_callback           = &State::on_mental_readiness_transition;
        c.mental_readiness_transition_ace_callback_user_data = state_.get();
    }
    if (o.on_lease_renewed) {
        c.lease_renewed_callback           = &State::on_lease_renewed;
        c.lease_renewed_callback_user_data = state_.get();
    }

    // output_dir stays null: the SDK writes no session data to disk.

    state_->handle = detail::check_handle(::theia_sdk_create(c), "TheiaSDK");
}

inline ::theia_sdk_t* TheiaSDK::handle() const
{
    if (!state_ || state_->handle == nullptr) {
        throw std::logic_error("TheiaSDK: used after being moved from");
    }
    return state_->handle;
}

inline TheiaSDK::~TheiaSDK()
{
    if (state_) { state_->shutdown(); }
}

inline TheiaSDK::TheiaSDK(TheiaSDK&& other) noexcept = default;

inline TheiaSDK& TheiaSDK::operator=(TheiaSDK&& other) noexcept
{
    if (this != &other) {
        if (state_) { state_->shutdown(); }
        state_ = std::move(other.state_);
    }
    return *this;
}

inline void TheiaSDK::start_new_session(const std::string& session_uuid)
{
    ::theia_sdk_t* h = handle();
    if (state_->session_open) { stop_processing(); }
    detail::check(::theia_sdk_start_session(h, session_uuid.c_str(), "Computer Task"),
                  "start_new_session");
    state_->session_open   = true;
    state_->feed_timed_out = false;
}

inline void TheiaSDK::start_realtime_data()
{
    ::theia_sdk_t* h = handle();
    State&         s = *state_;
    if (s.running) {
        throw std::logic_error("start_realtime_data: already running; call stop_processing() first");
    }

    if (s.tracker != nullptr) {
        s.tracker->start();
        try {
            s.start_feed();
        } catch (...) {
            try { s.tracker->close(); } catch (...) {}
            throw;
        }
    } else {
        if (s.options.device == Device::Screen || s.options.device == Device::Headset ||
            s.options.device == Device::Stasis || s.options.device == Device::VarjoXR3 ||
            s.options.device == Device::Quest) {
            throw std::logic_error(
                "start_realtime_data: this device needs a gaze source; call "
                "set_tracker() with your EyeTracker first, or skip "
                "start_realtime_data() and call add_gaze_samples()");
        }
        detail::check(::theia_sdk_start_data_streaming(h), "start_realtime_data");
        s.native_streaming = true;
    }
    s.running = true;
}

inline void TheiaSDK::stop_processing()
{
    ::theia_sdk_t* h = handle();
    State&         s = *state_;

    // Stop everything before reporting the first failure, so one stuck piece
    // does not leave the rest running.
    std::exception_ptr first;
    const auto attempt = [&first](auto&& step) {
        try { step(); } catch (...) { if (!first) { first = std::current_exception(); } }
    };

    s.stop_feed();
    if (s.running && s.tracker != nullptr) {
        attempt([&] { s.tracker->close(); });
    }
    if (s.native_streaming) {
        attempt([&] { detail::check(::theia_sdk_stop_data_streaming(h), "stop_processing"); });
    }
    if (s.video_started) {
        attempt([&] { detail::check(::theia_sdk_stop_video(h), "stop_processing"); });
    }
    if (s.session_open) {
        attempt([&] { detail::check(::theia_sdk_stop_session(h), "stop_processing"); });
    }
    s.running          = false;
    s.native_streaming = false;
    s.video_started    = false;
    s.session_open     = false;

    if (first) { std::rethrow_exception(first); }
}

inline bool TheiaSDK::is_running() const
{
    (void)handle();
    return state_->running;
}

inline void TheiaSDK::add_gaze_samples(const GazeSample* samples, std::size_t count)
{
    ::theia_sdk_t* h = handle();
    const State&   s = *state_;
    if (!s.session_open) {
        throw std::logic_error("add_gaze_samples: no session; call start_new_session() first");
    }
    if (s.native_streaming || s.feeding) {
        throw std::logic_error(
            "add_gaze_samples: the SDK is already reading gaze from start_realtime_data()");
    }
    if (count == 0) { return; }
    if (samples == nullptr) { throw std::invalid_argument("add_gaze_samples: samples is null"); }
    // The library copies each sample and never writes through this pointer,
    // despite its non-const signature.
    detail::check(::theia_sdk_add_gaze_samples(
                      h, const_cast<::gaze_sample_t*>(detail::as_c_samples(samples)), count),
                  "add_gaze_samples");
}

inline void TheiaSDK::add_gaze_samples(const std::vector<GazeSample>& samples)
{
    add_gaze_samples(samples.data(), samples.size());
}

inline void TheiaSDK::set_tracker(EyeTracker& tracker)
{
    (void)handle();
    if (state_->running) {
        throw std::logic_error("set_tracker: cannot change the tracker while running");
    }
    state_->tracker = &tracker;
}

inline void TheiaSDK::clear_tracker()
{
    (void)handle();
    if (state_->running) {
        throw std::logic_error("clear_tracker: cannot drop the tracker while running");
    }
    state_->tracker = nullptr;
}

inline void TheiaSDK::set_ganzin_connection(const std::string& ip, int port, int rtsp_port)
{
    detail::check(::theia_sdk_set_ganzin_connection(handle(), ip.c_str(), port, rtsp_port),
                  "set_ganzin_connection");
}

inline bool TheiaSDK::is_connected() const
{
    ::theia_sdk_t* h = handle();
    if (state_->tracker != nullptr) {
        return state_->feeding ? state_->tracker_connected.load() : state_->tracker->is_connected();
    }
    return detail::check_bool(::theia_sdk_is_tracker_connected(h), "is_connected");
}

inline bool TheiaSDK::has_connection_timeout() const
{
    ::theia_sdk_t* h = handle();
    if (state_->feed_timed_out) { return true; }
    return detail::check_bool(::theia_sdk_has_connection_timeout(h), "has_connection_timeout");
}

inline std::optional<CogLoadResult> TheiaSDK::get_cog_load() const
{
    ::cog_load_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_cog_load_ace(handle(), &c), "get_cog_load")) {
        return std::nullopt;
    }
    return detail::to_cog_load(c);
}

inline std::optional<FatigueResult> TheiaSDK::get_fatigue_level() const
{
    ::fatigue_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_fatigue(handle(), &c), "get_fatigue_level")) {
        return std::nullopt;
    }
    return detail::to_fatigue(c);
}

inline std::optional<AttentionResult> TheiaSDK::get_attention_style() const
{
    ::attention_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_attention_ace(handle(), &c),
                                "get_attention_style")) {
        return std::nullopt;
    }
    return detail::to_attention(c);
}

inline std::optional<MentalReadinessResult> TheiaSDK::get_mental_readiness(double elapsed_seconds) const
{
    ::mental_readiness_result_t c{};
    if (!detail::check_optional(
            ::theia_sdk_get_mental_readiness_ace(handle(), elapsed_seconds, &c),
            "get_mental_readiness")) {
        return std::nullopt;
    }
    return detail::to_mental_readiness(c);
}

inline std::vector<CogLoadResult> TheiaSDK::get_cog_load_prediction_history() const
{
    const ::cog_load_result_t* items = nullptr;
    std::size_t                count = 0;
    detail::check(::theia_sdk_get_cog_load_prediction_history(handle(), &items, &count),
                  "get_cog_load_prediction_history");

    // Copied immediately: the library reuses this buffer on the next call.
    std::vector<CogLoadResult> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) { out.push_back(detail::to_cog_load(items[i])); }
    return out;
}

inline void TheiaSDK::start_realtime_video()
{
    detail::check(::theia_sdk_start_video(handle()), "start_realtime_video");
    state_->video_started = true;
}

inline void TheiaSDK::stop_realtime_video()
{
    detail::check(::theia_sdk_stop_video(handle()), "stop_realtime_video");
    state_->video_started = false;
}

inline bool TheiaSDK::is_video_streaming() const
{
    return detail::check_bool(::theia_sdk_is_video_streaming(handle()), "is_video_streaming");
}

inline std::optional<VideoFrame> TheiaSDK::get_video_frame() const
{
    ::video_frame_t c{};
    if (!detail::check_optional(::theia_sdk_get_video_frame(handle(), &c), "get_video_frame")) {
        return std::nullopt;
    }
    return detail::to_video_frame(c);
}

inline void TheiaSDK::set_fatigue_time_offset_seconds(double seconds)
{
    detail::check(::theia_sdk_set_fatigue_time_offset_seconds(handle(), seconds),
                  "set_fatigue_time_offset_seconds");
}

inline void TheiaSDK::set_mental_readiness_min_session_seconds(double seconds)
{
    detail::check(::theia_sdk_set_mental_readiness_min_session_seconds(handle(), seconds),
                  "set_mental_readiness_min_session_seconds");
}

inline void TheiaSDK::set_graph_start_time(double unix_seconds)
{
    detail::check(::theia_sdk_set_graph_start_time(handle(), unix_seconds), "set_graph_start_time");
}

inline void TheiaSDK::set_cog_load_model_settings(const CogLoadModelSettings& settings)
{
    detail::check(::theia_sdk_set_cog_load_model_settings(
                      handle(), settings.consensus_window_size, settings.consensus_threshold,
                      settings.low_confidence_threshold, settings.high_confidence_threshold),
                  "set_cog_load_model_settings");
}

inline void TheiaSDK::set_mental_readiness_low_high_threshold(double threshold)
{
    detail::check(::theia_sdk_set_mental_readiness_low_high_threshold(handle(), threshold),
                  "set_mental_readiness_low_high_threshold");
}

inline void TheiaSDK::set_mental_readiness_report_windows(const std::vector<double>& nominal_window_seconds)
{
    // The library copies the list and never writes through this pointer,
    // despite its non-const signature.
    detail::check(::theia_sdk_set_mental_readiness_report_windows(
                      handle(), const_cast<double*>(nominal_window_seconds.data()),
                      nominal_window_seconds.size()),
                  "set_mental_readiness_report_windows");
}

inline std::vector<double> TheiaSDK::get_mental_readiness_report_windows() const
{
    const double* items = nullptr;
    std::size_t   count = 0;
    detail::check(::theia_sdk_get_mental_readiness_report_windows(handle(), &items, &count),
                  "get_mental_readiness_report_windows");

    // Copied immediately: the library reuses this buffer on the next call.
    return (items != nullptr && count != 0) ? std::vector<double>{items, items + count}
                                            : std::vector<double>{};
}

inline std::uint64_t TheiaSDK::subscribe_cog_load(
    std::vector<std::function<void(const CogLoadResult&)>> callbacks, const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<CogLoadResult, ::cog_load_result_t, &detail::to_cog_load>(
        &::theia_sdk_subscribe_cog_load_ace, std::move(callbacks), options, "subscribe_cog_load");
}

inline std::uint64_t TheiaSDK::subscribe_fatigue(
    std::vector<std::function<void(const FatigueResult&)>> callbacks, const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<FatigueResult, ::fatigue_result_t, &detail::to_fatigue>(
        &::theia_sdk_subscribe_fatigue, std::move(callbacks), options, "subscribe_fatigue");
}

inline std::uint64_t TheiaSDK::subscribe_attention(
    std::vector<std::function<void(const AttentionResult&)>> callbacks, const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<AttentionResult, ::attention_result_t, &detail::to_attention>(
        &::theia_sdk_subscribe_attention_ace, std::move(callbacks), options, "subscribe_attention");
}

inline std::uint64_t TheiaSDK::subscribe_mental_readiness(
    std::vector<std::function<void(const MentalReadinessWindowsResult&)>> callbacks,
    const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<MentalReadinessWindowsResult, ::mental_readiness_ace_result_t,
                             &detail::to_mental_readiness_windows>(
        &::theia_sdk_subscribe_mental_readiness_ace, std::move(callbacks), options,
        "subscribe_mental_readiness");
}

inline std::uint64_t TheiaSDK::subscribe_cog_load_transition(
    std::vector<std::function<void(const CogLoadTransitionEvent&)>> callbacks,
    const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<CogLoadTransitionEvent, ::cog_load_transition_event_t,
                             &detail::to_transition<CogLoadTransitionEvent, ::cog_load_transition_event_t>>(
        &::theia_sdk_subscribe_cog_load_transition_ace, std::move(callbacks), options,
        "subscribe_cog_load_transition");
}

inline std::uint64_t TheiaSDK::subscribe_fatigue_transition(
    std::vector<std::function<void(const FatigueTransitionEvent&)>> callbacks,
    const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<FatigueTransitionEvent, ::fatigue_transition_event_t,
                             &detail::to_transition<FatigueTransitionEvent, ::fatigue_transition_event_t>>(
        &::theia_sdk_subscribe_fatigue_transition_ace, std::move(callbacks), options,
        "subscribe_fatigue_transition");
}

inline std::uint64_t TheiaSDK::subscribe_attention_transition(
    std::vector<std::function<void(const AttentionTransitionEvent&)>> callbacks,
    const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<AttentionTransitionEvent, ::attention_transition_event_t,
                             &detail::to_transition<AttentionTransitionEvent, ::attention_transition_event_t>>(
        &::theia_sdk_subscribe_attention_transition_ace, std::move(callbacks), options,
        "subscribe_attention_transition");
}

inline std::uint64_t TheiaSDK::subscribe_mental_readiness_transition(
    std::vector<std::function<void(const MentalReadinessTransitionEvent&)>> callbacks,
    const SubscriptionOptions& options)
{
    (void)handle();
    return state_->subscribe<MentalReadinessTransitionEvent, ::mental_readiness_transition_event_t,
                             &detail::to_mental_readiness_transition>(
        &::theia_sdk_subscribe_mental_readiness_transition_ace, std::move(callbacks), options,
        "subscribe_mental_readiness_transition");
}

inline bool TheiaSDK::unsubscribe(std::uint64_t subscription_id)
{
    ::theia_sdk_t* h = handle();
    // Returns 1 removed, 0 unknown id, -1 failed.
    const int rc = ::theia_sdk_unsubscribe(h, subscription_id);
    if (rc == -1) { detail::throw_opaque_error("unsubscribe"); }
    if (rc != 1) { return false; }

    State& s = *state_;
    std::unique_ptr<detail::SubscriptionCallbacks> removed;
    {
        const std::lock_guard<std::mutex> lock(s.subscriptions_mutex);
        const auto it = s.subscriptions.find(subscription_id);
        if (it == s.subscriptions.end()) { return true; }
        removed = std::move(it->second);
        s.subscriptions.erase(it);
        // From inside a callback the library could not wait for it, and it
        // may be this subscription's, so it is kept until the SDK goes.
        if (detail::subscription_callback_depth() > 0) {
            s.unsubscribed_in_callback.push_back(std::move(removed));
        }
    }
    return true;  // `removed`, if still held, is freed here, outside the lock
}

inline std::vector<ModelInfo> TheiaSDK::get_model_info() const
{
    const ::model_info_t* items = nullptr;
    std::size_t           count = 0;
    detail::check(::theia_sdk_get_model_info(handle(), &items, &count), "get_model_info");

    std::vector<ModelInfo> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(ModelInfo{detail::copy_borrowed(items[i].name), items[i].version,
                                detail::copy_borrowed(items[i].commit)});
    }
    return out;
}

inline void TheiaSDK::process_baselining_data(double start_time_sec, double end_time_sec,
                                              const std::vector<GroundTruthPoint>& ground_truth)
{
    std::vector<::ground_truth_point_t> points = detail::to_c_ground_truth(ground_truth);
    detail::check(::theia_sdk_process_baselining_data(handle(), start_time_sec, end_time_sec,
                                                      points.data(), points.size()),
                  "process_baselining_data");
}

inline void TheiaSDK::process_general_model_baseline(double easy_task_start, double easy_task_end,
                                                     double hard_task_start, double hard_task_end,
                                                     int easy_task_gt, int hard_task_gt)
{
    detail::check(::theia_sdk_process_general_model_baseline(
                      handle(), easy_task_start, easy_task_end, hard_task_start, hard_task_end,
                      easy_task_gt, hard_task_gt),
                  "process_general_model_baseline");
}

inline void TheiaSDK::process_refining_data(const std::vector<GroundTruthPoint>& ground_truth,
                                            std::optional<double> baselining_end_time,
                                            const std::string&    refining_model)
{
    if (ground_truth.empty()) { return; }
    // The C layer reads a negative end time as "none".
    std::vector<::ground_truth_point_t> points = detail::to_c_ground_truth(ground_truth);
    detail::check(::theia_sdk_process_refining_data(handle(), points.data(), points.size(),
                                                    baselining_end_time.value_or(-1.0),
                                                    refining_model.c_str()),
                  "process_refining_data");
}

inline void TheiaSDK::save_refined_model(const std::string& path) const
{
    detail::check_succeeded(::theia_sdk_save_refined_model(handle(), path.c_str()), "save_refined_model");
}

inline void TheiaSDK::load_refined_model(const std::string& path)
{
    detail::check_succeeded(::theia_sdk_load_refined_model(handle(), path.c_str()), "load_refined_model");
}

inline void TheiaSDK::reset_to_base_model()
{
    detail::check_succeeded(::theia_sdk_reset_to_base_model(handle()), "reset_to_base_model");
}

inline void TheiaSDK::set_alert_rules(const std::string& rules_json)
{
    detail::check(::theia_sdk_set_alert_rules(handle(), rules_json.c_str()), "set_alert_rules");
}

inline void TheiaSDK::set_alert_callback(AlertCallback callback)
{
    ::theia_sdk_t* h = handle();

    // Unregister first: the library waits out any delivery already under way,
    // so nothing is reading alert_callback while it is replaced. Called from
    // inside the callback, the library skips that wait; that delivery holds
    // its own reference to the callback it is running, which therefore lives
    // until it returns.
    detail::check(::theia_sdk_set_alert_callback(h, nullptr, nullptr), "set_alert_callback");

    state_->alert_callback =
        callback ? std::make_shared<const AlertCallback>(std::move(callback)) : nullptr;
    if (state_->alert_callback) {
        detail::check(::theia_sdk_set_alert_callback(h, &State::on_alert, state_.get()),
                      "set_alert_callback");
    }
}

inline std::vector<AlertRuleInfo> TheiaSDK::list_alert_rules() const
{
    const ::alert_rule_info_t* rules = nullptr;
    std::size_t                count = 0;
    detail::check(::theia_sdk_list_alert_rules(handle(), &rules, &count), "list_alert_rules");

    std::vector<AlertRuleInfo> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) { out.push_back(detail::to_alert_rule_info(rules[i])); }
    return out;
}

inline AlertRulesStatus TheiaSDK::alert_rules_status() const
{
    return detail::to_alert_rules_status(::theia_sdk_get_alert_rules_status(handle()));
}

inline void TheiaSDK::set_alert_subject(const std::string& subject)
{
    detail::check(::theia_sdk_set_alert_subject(handle(), subject.c_str()), "set_alert_subject");
}

inline void TheiaSDK::enable_alert_sync(const std::string& api_base_url, int interval_seconds,
                                        bool report_firings)
{
    detail::check(::theia_sdk_enable_alert_sync(handle(), api_base_url.c_str(), interval_seconds,
                                                report_firings ? 1 : 0),
                  "enable_alert_sync");
}

inline void TheiaSDK::disable_alert_sync()
{
    detail::check(::theia_sdk_disable_alert_sync(handle()), "disable_alert_sync");
}

inline void TheiaSDK::refresh_alert_rules()
{
    detail::check(::theia_sdk_refresh_alert_rules(handle()), "refresh_alert_rules");
}

inline void TheiaSDK::set_lease_token(const std::string& lease_token)
{
    if (::theia_sdk_set_lease_token(handle(), lease_token.c_str()) != 0) {
        detail::throw_recorded_error("set_lease_token");
    }
}

inline void TheiaSDK::renew_lease()
{
    if (::theia_sdk_renew_lease(handle()) != 0) { detail::throw_recorded_error("renew_lease"); }
}

inline LeaseInfo TheiaSDK::lease_info() const
{
    const ::lease_info_t c = ::theia_sdk_get_lease_info(handle());
    LeaseInfo info;
    info.has_lease                    = c.has_lease != 0;
    info.device_id                    = detail::copy_borrowed(c.device_id);
    info.tenant_id                    = detail::copy_borrowed(c.tenant_id);
    info.lease_id                     = detail::copy_borrowed(c.lease_id);
    info.issued_at                    = c.issued_at;
    info.refresh_after                = c.refresh_after;
    info.warn_after                   = c.warn_after;
    info.expires_at                   = c.expires_at;
    info.auto_renew                   = c.auto_renew != 0;
    info.can_activate                 = c.can_activate != 0;
    info.lease_file                   = detail::copy_borrowed(c.lease_file);
    info.last_renewal_attempt_unix_ms = c.last_renewal_attempt_unix_ms;
    info.last_renewal_error           = detail::copy_borrowed(c.last_renewal_error);
    return info;
}

}  // namespace harmoneyes::theia
