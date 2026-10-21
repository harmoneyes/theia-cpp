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
#undef HARMONEYES_THEIA_SAME_FIELD

}  // namespace harmoneyes::theia

namespace harmoneyes::theia::detail {

// ── Strings from the library ──
//
// Every `const char*` the library returns points into storage it reuses on
// the next call, so each one is copied before it is returned.

[[nodiscard]] inline std::string copy_borrowed(const char* s)
{
    return s ? std::string{s} : std::string{};
}

// ── Return codes ──
//
// Only creating the SDK and setting the lease record why they failed; every
// other call reports failure with -1 alone. So the recorded reason is read
// only after those two, never after anything else, where it would describe
// some earlier failure.
//
// Nothing here may be called from a callback trampoline: an exception must
// not unwind through the library's frames.

/// Throw the recorded failure. Only valid straight after a failed
/// theia_sdk_create() or theia_sdk_set_lease_token() on this thread.
[[noreturn]] inline void throw_recorded_error(const char* context)
{
    const int   code   = static_cast<int>(::theia_get_last_error_code());
    const char* reason = ::theia_get_last_error_message();
    const std::string message =
        std::string{context} + ": " + ((reason && *reason) ? reason : "no detail reported");

    switch (code) {
        case THEIA_ERROR_MODEL_UNAVAILABLE:     throw ModelUnavailableError{message};
        case THEIA_ERROR_LEASE_MALFORMED:       throw LeaseMalformedError{message};
        case THEIA_ERROR_LEASE_UNKNOWN_KEY:     throw LeaseUnknownKeyError{message};
        case THEIA_ERROR_LEASE_BAD_SIGNATURE:   throw LeaseSignatureError{message};
        case THEIA_ERROR_LEASE_EXPIRED:         throw LeaseExpiredError{message};
        case THEIA_ERROR_LEASE_DEVICE_MISMATCH: throw LeaseDeviceMismatchError{message};
        case THEIA_ERROR_LEASE_TENANT_MISMATCH: throw LeaseTenantMismatchError{message};
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

// ── Conversions ──

/// The platform name the library expects for a device.
[[nodiscard]] inline const char* platform_string(Device device) noexcept
{
    switch (device) {
        case Device::Ganzin: return "Ganzin";
        case Device::Webcam: return "Webcam";
        case Device::PupilLabsNeon:
            break;
    }
    return "PL";
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
    return r;
}

[[nodiscard]] inline AttentionResult to_attention(const ::attention_result_t& c)
{
    AttentionResult r;
    r.level              = static_cast<AttentionLevel>(c.level);
    r.label              = copy_borrowed(c.label);
    r.composite_score    = c.composite_score;
    r.total_observations = c.total_observations;
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
    AlertCallback  alert_callback;
    ::theia_sdk_t* handle = nullptr;

    bool session_open      = false;
    bool running           = false;
    bool native_streaming  = false;
    bool video_started     = false;

    EyeTracker*       tracker = nullptr;
    std::thread       feed;
    std::atomic<bool> feeding{false};
    std::atomic<bool> feed_timed_out{false};

    /// The tracker's own is_connected(), sampled by the feed thread. Read
    /// through this rather than calling the tracker directly: EyeTracker is
    /// documented as being used from one background thread, so asking it
    /// from the caller's thread would race with get_buffered_data().
    std::atomic<bool> tracker_connected{false};

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
        feed_timed_out = false;
        feeding        = true;
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

    static void on_alert(::alert_event_t event, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->alert_callback) { return; }
        try { self->alert_callback(detail::to_alert_event(event)); } catch (...) {}
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

    if (o.lease_token.empty()) {
        if (const char* v = std::getenv("THEIA_LEASE"); v != nullptr && *v != '\0') {
            o.lease_token = v;
        } else if (const char* k = std::getenv("THEIA_LICENSE_KEY"); k != nullptr) {
            o.lease_token = k;
        }
    }

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

    if (o.on_log)      { c.log_callback = &State::on_log;               c.log_callback_user_data = state_.get(); }
    if (o.on_cog_load) { c.cog_load_ace_callback = &State::on_cog_load; c.cog_load_ace_callback_user_data = state_.get(); }
    if (o.on_fatigue)  { c.fatigue_callback = &State::on_fatigue;       c.fatigue_callback_user_data = state_.get(); }

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
        if (s.options.device == Device::Webcam) {
            throw std::logic_error(
                "start_realtime_data: Device::Webcam needs a gaze source; call "
                "set_tracker() with your EyeTracker first");
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

inline void TheiaSDK::set_tracker(EyeTracker& tracker)
{
    (void)handle();
    if (state_->running) {
        throw std::logic_error("set_tracker: cannot change the tracker while running");
    }
    state_->tracker           = &tracker;
    state_->tracker_connected = tracker.is_connected();
}

inline void TheiaSDK::clear_tracker()
{
    (void)handle();
    if (state_->running) {
        throw std::logic_error("clear_tracker: cannot drop the tracker while running");
    }
    state_->tracker           = nullptr;
    state_->tracker_connected = false;
}

inline void TheiaSDK::set_ganzin_connection(const std::string& ip, int port, int rtsp_port)
{
    detail::check(::theia_sdk_set_ganzin_connection(handle(), ip.c_str(), port, rtsp_port),
                  "set_ganzin_connection");
}

inline bool TheiaSDK::is_connected() const
{
    ::theia_sdk_t* h = handle();
    if (state_->tracker != nullptr) { return state_->tracker_connected.load(); }
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
    // so nothing is reading alert_callback while it is replaced. (Called from
    // inside the callback, the library skips that wait, and the assignment
    // then happens on the delivery thread itself, which is equally safe.)
    detail::check(::theia_sdk_set_alert_callback(h, nullptr, nullptr), "set_alert_callback");

    state_->alert_callback = std::move(callback);
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

}  // namespace harmoneyes::theia
