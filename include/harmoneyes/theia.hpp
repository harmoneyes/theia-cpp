#pragma once

/// The HarmonEyes Theia SDK for C++.
///
/// This one header is the whole API.
///
///     #include <harmoneyes/theia.hpp>
///     namespace het = harmoneyes::theia;
///
///     het::TheiaSDK sdk{het::Device::PupilLabsNeon};   // lease from $THEIA_LEASE
///     sdk.start_new_session(session_uuid);
///     sdk.start_realtime_data();
///     ...poll sdk.get_cog_load() and sdk.get_fatigue_level() once a second...
///     sdk.stop_processing();
///
/// Call a TheiaSDK from one thread at a time. add_gaze_samples() is the one
/// exception; see it.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct theia_sdk_t;

namespace harmoneyes::theia {

/// Version of these headers. version() is the native library you are
/// running; include both when reporting a problem.
inline constexpr std::string_view kVersion = "1.1.0";

/// Version of the native Theia library.
[[nodiscard]] std::string version();

// -- Errors ---------------------------------------------------------------

/// Base of every exception the SDK throws for a failure at run time.
/// Misuse, such as calling start_realtime_data() twice, throws
/// std::logic_error instead.
class Error : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// A model could not be loaded: not cached and not downloadable, or damaged.
class ModelUnavailableError : public Error { using Error::Error; };

/// The device lease was refused. Catch this to handle any lease problem, or
/// one of the subclasses below to tell them apart.
class LeaseVerificationError : public Error { using Error::Error; };

/// The lease is not a well-formed token.
class LeaseMalformedError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };
/// The lease was signed with a key this build does not know.
class LeaseUnknownKeyError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };
/// The lease signature did not verify.
class LeaseSignatureError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };
/// The lease has expired. Request a new one; a running process can swap it
/// in with TheiaSDK::set_lease_token().
class LeaseExpiredError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };
/// The lease was issued for a different device.
class LeaseDeviceMismatchError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };
/// The lease was issued for a different tenant.
class LeaseTenantMismatchError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };

// -- Results --------------------------------------------------------------

/// Cognitive load band.
enum class CogLoadLevel : int { Low = 0, Moderate = 1, High = 2 };

/// Fatigue band, from the Karolinska Sleepiness Scale.
enum class FatigueLevel : int {
    Alert            = 0,  // KSS 1-4
    SomewhatFatigued = 1,  // KSS 5-6
    Fatigued         = 2,  // KSS 7-9
};

/// Attentional style, from narrow focus to broad scanning. Numbered from 1.
enum class AttentionLevel : int {
    Narrow         = 1,
    ModerateNarrow = 2,
    Neither        = 3,
    ModerateBroad  = 4,
    Broad          = 5,
};

/// "Low", "Moderate" or "High".
[[nodiscard]] const char* to_string(CogLoadLevel level) noexcept;

/// "Alert", "Somewhat Fatigued" or "Fatigued".
[[nodiscard]] const char* to_string(FatigueLevel level) noexcept;

/// A cognitive-load prediction.
struct CogLoadResult {
    CogLoadLevel level        = CogLoadLevel::Low;
    std::string  label;                ///< "Low", "Moderate" or "High"
    float        confidence   = 0.0F;  ///< 0..1
    int          batch_number = 0;
    std::int64_t timestamp_ns = 0;
};

/// A fatigue prediction.
struct FatigueResult {
    FatigueLevel                level        = FatigueLevel::Alert;
    std::string                 label;              ///< "Alert", "Somewhat Fatigued" or "Fatigued"
    float                       confidence   = 0.0F;  ///< 0..1
    int                         batch_number = 0;
    std::optional<FatigueLevel> ttt_level;    ///< the level fatigue is heading towards
    std::optional<int>          ttt_minutes;  ///< estimated minutes until it gets there
};

/// An attentional-style classification.
struct AttentionResult {
    AttentionLevel level              = AttentionLevel::Neither;
    std::string    label;
    double         composite_score    = 0.0;  ///< 0..1
    int            total_observations = 0;
};

/// How cognitive load has been distributed over the session so far.
/// The three percentages sum to 100.
struct MentalReadinessResult {
    double low_percentage      = 0.0;
    double moderate_percentage = 0.0;
    double high_percentage     = 0.0;
    int    total_predictions   = 0;
};

/// One video frame. Carries raw BGR pixels, an encoded packet, or both,
/// depending on the tracker; either vector may be empty.
struct VideoFrame {
    std::vector<std::uint8_t> bgr_data;
    std::vector<std::uint8_t> encoded_data;
    std::vector<std::uint8_t> extradata;
    int    width                  = 0;
    int    height                 = 0;
    double timestamp_unix_seconds = 0.0;
    bool   is_keyframe            = false;
};

/// One loaded model's identity.
struct ModelInfo {
    std::string   name;
    std::uint32_t version = 0;
    std::string   commit;
};

/// A labelled moment, for personalising the cognitive-load model.
struct GroundTruthPoint {
    double time_unix_seconds = 0.0;
    int    value             = 0;  ///< the participant's rating at that moment
};

/// Cognitive-load smoothing and thresholds. The defaults are the model's own.
struct CogLoadModelSettings {
    int   consensus_window_size     = 15;
    float consensus_threshold       = 0.6F;
    float low_confidence_threshold  = 0.55F;
    float high_confidence_threshold = 0.70F;
};

// -- Alerts ---------------------------------------------------------------

/// One alert that fired.
struct AlertEvent {
    std::string  firing_id;
    std::string  rule_id;
    std::string  rule_name;
    std::string  input;                    ///< which signal the rule watches
    int          level               = 0;
    int          previous_level      = 0;
    std::string  trigger;                  ///< what caused the firing
    int          sustained_sec       = 0;
    int          level_share_percent = 0;
    std::string  message;
    std::int64_t timestamp_ns        = 0;
    std::int64_t fired_at_unix_ms    = 0;
};

/// A rule currently being evaluated.
struct AlertRuleInfo {
    std::string  id;
    std::string  name;
    std::string  summary;
    std::string  input;
    std::string  input_name;
    std::string  levels;
    bool         on_increase            = false;
    bool         on_decrease            = false;
    int          min_duration_sec       = 0;
    int          level_share_percent    = 0;
    int          level_share_window_sec = 0;
    int          cooldown_sec           = 0;
    int          readiness_window_sec   = 0;
    std::string  source;
    std::int64_t last_fired_at_unix_ms  = 0;
};

/// Where the rules came from and how synchronisation stands.
struct AlertRulesStatus {
    std::string  source;
    int          rule_count                 = 0;
    int          skipped_rule_count         = 0;
    std::string  skipped_detail;            ///< why a rule was left out
    std::string  etag;
    std::string  generated_at;
    std::string  tenant_id;
    std::string  subject;
    std::int64_t loaded_at_unix_ms          = 0;
    std::string  last_error;
    std::string  sync_state;
    std::string  sync_url;
    std::int64_t last_sync_attempt_unix_ms  = 0;
    std::int64_t last_sync_success_unix_ms  = 0;
    std::string  sync_error;
    int          pending_firings            = 0;
    std::int64_t last_firing_upload_unix_ms = 0;
    std::string  firing_upload_error;
};

/// Called once per fired alert, on the SDK's thread rather than yours.
using AlertCallback = std::function<void(const AlertEvent&)>;

// -- Devices and gaze sources ---------------------------------------------

/// Which eye tracker the SDK is set up for.
enum class Device {
    PupilLabsNeon,  ///< Pupil Labs Neon, discovered on the network automatically
    Ganzin,         ///< Ganzin Sol; call set_ganzin_connection() first
    Screen,         ///< A screen-based tracker you supply; see EyeTracker
    Headset,        ///< A head-mounted tracker you supply, reporting gaze direction; see GazeSample
};

/// Marks a measurement as absent. A real number, not a NaN, so it compares
/// equal and must be tested for explicitly.
inline constexpr double NON_COMPUTE_VALUE = -8888.0;

/// One gaze sample, for feeding your own gaze source into the SDK.
///
/// Every field starts at its "not measured" value, so fill in only what your
/// tracker provides.
///
/// Device::Headset reads only timestamp_ns, the optical axes and the pupil
/// diameters, and ignores the rest.
struct GazeSample {
    std::int64_t timestamp_ns = 0;  ///< capture time, nanoseconds since the Unix epoch

    // Gaze position on screen, in pixels.
    float left_eye_x  = 0.0F;
    float left_eye_y  = 0.0F;
    float right_eye_x = 0.0F;
    float right_eye_y = 0.0F;

    // Pupil diameter, in millimetres.
    double pupil_diameter_left  = NON_COMPUTE_VALUE;
    double pupil_diameter_right = NON_COMPUTE_VALUE;
    double pupil_diameter       = NON_COMPUTE_VALUE;  ///< average of left and right

    float depth = 0.0F;

    // Per-eye state.
    std::int8_t  left_eye_validity    = 1;     ///< 1 = valid, 0 = invalid
    std::int8_t  right_eye_validity   = 1;
    std::int8_t  left_eye_status      = 2;     ///< 0 = lost, 1 = blink, 2 = open
    std::int8_t  right_eye_status     = 2;
    float        left_eye_confidence  = 1.0F;  ///< 0..1
    float        right_eye_confidence = 1.0F;
    std::int32_t blink_id             = -1;    ///< which blink this sample belongs to; -1 = none

    // 3D eye model: eyeball centres and optical-axis directions. For
    // Device::Headset, an optical axis is the eye's gaze direction as a unit
    // vector in the head's frame (+x right, +y up, +z forward), and (0, 0, 0)
    // means that eye was not tracked.
    float eyeball_center_left_x  = 0.0F;
    float eyeball_center_left_y  = 0.0F;
    float eyeball_center_left_z  = 0.0F;
    float optical_axis_left_x    = 0.0F;
    float optical_axis_left_y    = 0.0F;
    float optical_axis_left_z    = 0.0F;
    float eyeball_center_right_x = 0.0F;
    float eyeball_center_right_y = 0.0F;
    float eyeball_center_right_z = 0.0F;
    float optical_axis_right_x   = 0.0F;
    float optical_axis_right_y   = 0.0F;
    float optical_axis_right_z   = 0.0F;

    // Eyelids: angles in degrees, aperture (openness) in millimetres.
    float eyelid_angle_top_left     = -8888.0F;
    float eyelid_angle_bottom_left  = -8888.0F;
    float eyelid_angle_top_right    = -8888.0F;
    float eyelid_angle_bottom_right = -8888.0F;
    float eyelid_aperture_left_mm   = -8888.0F;
    float eyelid_aperture_right_mm  = -8888.0F;

    // Head motion: acceleration in m/s^2, rotation rate in degrees/s, and
    // orientation as a unit quaternion.
    float accel_x = -8888.0F;
    float accel_y = -8888.0F;
    float accel_z = -8888.0F;
    float gyro_x  = -8888.0F;
    float gyro_y  = -8888.0F;
    float gyro_z  = -8888.0F;
    float quat_w  = -8888.0F;
    float quat_x  = -8888.0F;
    float quat_y  = -8888.0F;
    float quat_z  = -8888.0F;

    // Gaze normalised to [0, 1] from the top-left of the screen. Needed for
    // screen-based trackers.
    float gaze_x_norm = -8888.0F;
    float gaze_y_norm = -8888.0F;

    // The display the gaze was measured on. Set all four, or none.
    float        screen_width_mm      = -8888.0F;
    float        screen_height_mm     = -8888.0F;
    std::int32_t screen_res_width_px  = -1;
    std::int32_t screen_res_height_px = -1;

    // Head distance from the screen, in centimetres.
    double head_pose_depth_cm = NON_COMPUTE_VALUE;
    double head_pose_tz       = NON_COMPUTE_VALUE;
};

/// A gaze source the SDK reads from, rather than a device it connects to
/// itself. Implement this to feed the SDK from a tracker of your own.
///
/// Give it to TheiaSDK::set_tracker(). start_realtime_data() calls start(),
/// then polls get_buffered_data() and is_connected() on the SDK's own thread;
/// stop_processing() stops polling and calls close(). start() and close() run
/// on the thread that called the SDK. The SDK never calls two of these at
/// once, so they may share state without locking.
class EyeTracker {
public:
    virtual ~EyeTracker() = default;

    EyeTracker()                             = default;
    EyeTracker(const EyeTracker&)            = default;
    EyeTracker& operator=(const EyeTracker&) = default;
    EyeTracker(EyeTracker&&)                 = default;
    EyeTracker& operator=(EyeTracker&&)      = default;

    /// Begin producing samples.
    virtual void start() {}

    /// Samples gathered since the last call. Returning none is normal. Must
    /// not block for long.
    [[nodiscard]] virtual std::vector<GazeSample> get_buffered_data() = 0;

    /// Whether the tracker still has its device.
    [[nodiscard]] virtual bool is_connected() const = 0;

    /// Stop producing samples and release the device.
    virtual void close() {}
};

// -- Options --------------------------------------------------------------

/// How to construct the SDK. Every field has a usable default.
///
/// The callbacks run on the SDK's own threads, not yours, so anything they
/// touch needs its own synchronisation. Unlike the get_* calls, they never
/// miss a result.
struct Options {
    Device device = Device::PupilLabsNeon;

    /// Your device lease. Left empty, the SDK reads $THEIA_LEASE, then
    /// $THEIA_LICENSE_KEY.
    std::string lease_token;
    std::string lease_expected_device_id;  ///< optional: require the lease to name this device
    std::string lease_expected_tenant_id;  ///< optional: require the lease to name this tenant

    bool enable_cog_load_model = true;
    bool enable_fatigue_model  = true;

    /// Where downloaded models are cached. Empty uses $THEIA_MODEL_CACHE_DIR,
    /// then ~/.theia/models.
    std::string model_cache_dir;

    std::function<void(const std::string&)>   on_log;
    std::function<void(const CogLoadResult&)> on_cog_load;
    std::function<void(const FatigueResult&)> on_fatigue;
};

// -- The SDK --------------------------------------------------------------

/// The eye-tracking pipeline. Move-only; destroying it stops everything.
class TheiaSDK {
public:
    /// Throws a LeaseVerificationError subclass if the lease is missing or
    /// invalid, or ModelUnavailableError if a model cannot be loaded.
    explicit TheiaSDK(Device device);
    explicit TheiaSDK(Options options);
    ~TheiaSDK();

    TheiaSDK(TheiaSDK&& other) noexcept;
    TheiaSDK& operator=(TheiaSDK&& other) noexcept;
    TheiaSDK(const TheiaSDK&)            = delete;
    TheiaSDK& operator=(const TheiaSDK&) = delete;

    // -- Sessions --

    /// Begin a session, stopping any session already open.
    void start_new_session(const std::string& session_uuid);

    /// Start collecting and processing gaze. Pupil Labs and Ganzin connect to
    /// the device; with a tracker from set_tracker(), this starts it and
    /// feeds its samples in on a background thread.
    void start_realtime_data();

    /// Stop collecting, stop any tracker and video, and end the session.
    void stop_processing();

    [[nodiscard]] bool is_running() const;

    // -- Device connection --

    /// Use this gaze source. Device::Screen and Device::Headset need either
    /// this or add_gaze_samples(). It cannot be changed while running.
    ///
    /// The tracker must outlive the SDK, or be dropped with clear_tracker()
    /// first. Declaring it *after* the SDK destroys it first and is the easy
    /// way to get this wrong:
    ///
    ///     MyTracker     tracker;   // declare the tracker FIRST
    ///     het::TheiaSDK sdk{opts};
    ///     sdk.set_tracker(tracker);
    void set_tracker(EyeTracker& tracker);

    /// Stop using the tracker given to set_tracker(). Call this before
    /// destroying it if it does not outlive the SDK. Throws while running.
    void clear_tracker();

    /// Where the Ganzin Sol is. Call before start_realtime_data().
    void set_ganzin_connection(const std::string& ip, int port = 8080, int rtsp_port = 8086);

    /// Whether the device, or the tracker from set_tracker(), is still
    /// connected. Samples you push with add_gaze_samples() have no connection
    /// for it to report on.
    [[nodiscard]] bool is_connected() const;

    /// Whether the device or tracker stopped delivering samples. Like
    /// is_connected(), it does not cover add_gaze_samples().
    [[nodiscard]] bool has_connection_timeout() const;

    // -- Gaze you collect yourself --

    /// Feed in gaze samples as your tracker delivers them: any number per
    /// call, in time order. For Device::Screen and Device::Headset, in place
    /// of set_tracker() and start_realtime_data(): start a session, then call
    /// this until stop_processing().
    ///
    /// It may run on another thread, such as your tracker's own callback,
    /// while you call the get_* methods, but not alongside any other call,
    /// itself included. Most calls return in microseconds; about once a
    /// second one takes a few milliseconds to score the newest data, so keep
    /// it off render and UI threads.
    ///
    /// Throws std::logic_error before start_new_session(), and while the SDK
    /// is reading a device or a tracker of its own.
    void add_gaze_samples(const GazeSample* samples, std::size_t count);
    void add_gaze_samples(const std::vector<GazeSample>& samples);

    // -- Results --
    //
    // get_cog_load() and get_fatigue_level() return the newest result and
    // clear it, so a second call before the next result returns nothing.
    // get_attention_style() and get_mental_readiness() report the running
    // state and return the same value until it changes.

    [[nodiscard]] std::optional<CogLoadResult>   get_cog_load() const;
    [[nodiscard]] std::optional<FatigueResult>   get_fatigue_level() const;
    [[nodiscard]] std::optional<AttentionResult> get_attention_style() const;

    /// `elapsed_seconds` of 0 uses the session clock.
    [[nodiscard]] std::optional<MentalReadinessResult> get_mental_readiness(double elapsed_seconds = 0.0) const;

    /// Every cognitive-load prediction this session, oldest first.
    [[nodiscard]] std::vector<CogLoadResult> get_cog_load_prediction_history() const;

    // -- Scene video, for trackers that provide it --

    void start_realtime_video();
    void stop_realtime_video();
    [[nodiscard]] bool is_video_streaming() const;

    /// The next frame, or nothing if none is ready yet.
    [[nodiscard]] std::optional<VideoFrame> get_video_frame() const;

    // -- Tuning --

    /// How long a session must run before get_mental_readiness() reports.
    void set_mental_readiness_min_session_seconds(double seconds);

    /// Shift the clock the fatigue model reads time of day from, for example
    /// when replaying a recording captured at a different hour.
    void set_fatigue_time_offset_seconds(double seconds);

    /// Cognitive-load smoothing and confidence thresholds.
    void set_cog_load_model_settings(const CogLoadModelSettings& settings);

    // -- Personalising the cognitive-load model --
    //
    // Times are in seconds from set_graph_start_time(); ground-truth points
    // carry their own Unix times.

    void set_graph_start_time(double unix_seconds);
    void process_baselining_data(double start_time_sec, double end_time_sec,
                                 const std::vector<GroundTruthPoint>& ground_truth);
    void process_general_model_baseline(double easy_task_start, double easy_task_end,
                                        double hard_task_start, double hard_task_end,
                                        int easy_task_gt = 1, int hard_task_gt = 6);
    void process_refining_data(const std::vector<GroundTruthPoint>& ground_truth,
                               std::optional<double> baselining_end_time = std::nullopt,
                               const std::string&    refining_model      = "General");
    void save_refined_model(const std::string& path) const;
    void load_refined_model(const std::string& path);
    void reset_to_base_model();

    /// Which models are loaded, and at what version.
    [[nodiscard]] std::vector<ModelInfo> get_model_info() const;

    // -- Alerts --

    void set_alert_rules(const std::string& rules_json);

    /// Called once per fired alert, on the SDK's thread.
    void set_alert_callback(AlertCallback callback);
    [[nodiscard]] std::vector<AlertRuleInfo> list_alert_rules() const;
    [[nodiscard]] AlertRulesStatus alert_rules_status() const;
    void set_alert_subject(const std::string& subject);

    /// Keep alert rules in sync with your HarmonEyes dashboard. An empty
    /// `api_base_url` uses the default.
    void enable_alert_sync(const std::string& api_base_url = {}, int interval_seconds = 60,
                           bool report_firings = true);
    void disable_alert_sync();
    void refresh_alert_rules();

    // -- Lease --

    /// Swap in a renewed lease without restarting. Keeps the old lease and
    /// throws a LeaseVerificationError subclass if the new one is rejected.
    void set_lease_token(const std::string& lease_token);

private:
    struct State;
    [[nodiscard]] ::theia_sdk_t* handle() const;

    std::shared_ptr<State> state_;
};

}  // namespace harmoneyes::theia

// Definitions. Header-only, so they live in a header, but nothing in there is
// part of the API.
#define HARMONEYES_THEIA_INCLUDING_IMPL
#include "harmoneyes/theia/detail/sdk_impl.hpp"
#undef HARMONEYES_THEIA_INCLUDING_IMPL
