#pragma once

/// The HarmonEyes Theia SDK for C++.
///
/// This one header is the whole API.
///
///     #include <harmoneyes/theia.hpp>
///     namespace het = harmoneyes::theia;
///
///     het::TheiaSDK sdk{het::Device::PupilLabsNeon};   // lease: see README, "Device lease"
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
inline constexpr std::string_view kVersion = "2.3.0";

/// Version of the native Theia library.
[[nodiscard]] std::string version();

/// The name this machine registers under when the SDK obtains its own lease:
/// "theia-" and 32 hex digits, a hash of the OS machine ID (which never
/// leaves the machine). Empty where the OS has none; set
/// Options::lease_device_identifier there.
[[nodiscard]] std::string machine_device_identifier();

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
/// HarmonEyes could not renew the lease or issue a new one: it refused (the
/// device was removed, the API key revoked) or could not be reached. The
/// message says which. Any lease the SDK already holds is kept.
class LeaseRenewalError : public LeaseVerificationError { using LeaseVerificationError::LeaseVerificationError; };

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

/// Mental readiness band for one report window. High means the window was
/// mostly not at high cognitive load: its mental_readiness_percentage reached
/// the threshold set by TheiaSDK::set_mental_readiness_low_high_threshold().
enum class MentalReadinessLevel : int { Low = 0, High = 1 };

/// Why a level-change event fired.
enum class TransitionReason : int {
    SessionInitial = 0,  ///< the session's first level; previous_level equals new_level
    Changed        = 1,  ///< the level changed
};

// Durations below are in seconds of gaze time (the samples' timestamps), and
// restart with each session.

/// A cognitive-load prediction.
struct CogLoadResult {
    CogLoadLevel level        = CogLoadLevel::Low;
    std::string  label;                ///< "Low", "Moderate" or "High"
    float        confidence   = 0.0F;  ///< 0..1
    int          batch_number = 0;
    std::int64_t timestamp_ns = 0;
    float        prob_high    = 0.0F;  ///< the model's probability of high load, 0..1
    float        prob_low     = 0.0F;  ///< the model's probability of low load, 0..1
    double       level_duration_sec = 0.0;  ///< how long this level has held; 0 on its first prediction
    std::string  level_label;          ///< level's name, from the SDK: "Low", "Moderate" or "High"
};

/// A fatigue prediction.
struct FatigueResult {
    FatigueLevel                level        = FatigueLevel::Alert;
    std::string                 label;              ///< "Alert", "Somewhat Fatigued" or "Fatigued"
    float                       confidence   = 0.0F;  ///< 0..1
    int                         batch_number = 0;
    std::optional<FatigueLevel> ttt_level;    ///< the level fatigue is heading towards
    std::optional<int>          ttt_minutes;  ///< estimated minutes until it gets there
    double                      level_duration_sec = 0.0;  ///< how long this level has held; 0 on its first prediction
    std::string                 level_label;  ///< level's name, from the SDK: "Alert", "Somewhat Fatigued" or "Fatigued"
};

/// An attentional-style classification.
struct AttentionResult {
    AttentionLevel level              = AttentionLevel::Neither;
    std::string    label;
    double         composite_score    = 0.0;  ///< 0..1
    int            total_observations = 0;
    double         level_duration_sec = 0.0;  ///< how long this level has held; 0 on its first classification
    std::string    level_label;               ///< level's name, from the SDK
};

/// Eye strain: a 0-100 score that rises with time at the screen (100 after
/// ramp_seconds, 20 minutes) and resets after a break (break_seconds, 20 s,
/// in a row away), the 20-20-20 rule. Device::Screen (the webcam) and
/// Device::Stasis only.
/// On Stasis, time at the screen is the time the camera tracks the eye with
/// gaze near the wearer's usual direction, and a break is break_seconds with
/// the eye closed or lost, or looking well away from it. A head turned to
/// look away, with the eye centred, still counts as screen time, so treat the
/// breaks it finds as fewer than the wearer took. It is a timer, not a
/// measure of the eye.
struct EyeStrainResult {
    double       score                  = 0.0;    ///< now, 0-100
    std::int64_t on_seconds_since_break = 0;      ///< now: screen time since the last break, not capped
    bool         in_break               = false;  ///< now: away for break_seconds or more
    std::int64_t on_seconds             = 0;      ///< the session's screen time
    std::int64_t extra_screen_seconds   = 0;      ///< of it, on a monitor beside the main one (webcam)
    std::int64_t on_seconds_at_max      = 0;      ///< of it, with the score at 100
    int          break_count            = 0;      ///< breaks taken this session
    int          max_reached_count      = 0;      ///< times the score reached 100
    std::int64_t longest_on_seconds     = 0;      ///< the most screen time between two breaks
    double       ramp_seconds           = 1200.0;
    int          break_seconds          = 20;
};

/// How cognitive load has been distributed over the session so far.
/// The three percentages sum to 100.
struct MentalReadinessResult {
    double low_percentage      = 0.0;
    double moderate_percentage = 0.0;
    double high_percentage     = 0.0;
    int    total_predictions   = 0;
};

/// Marks a report window as covering the whole session; see
/// TheiaSDK::set_mental_readiness_report_windows().
inline constexpr double kWholeSessionWindow = -1.0;

/// Mental readiness over one report window.
struct MentalReadinessWindowMeasurement {
    double       nominal_window_seconds    = kWholeSessionWindow;  ///< the window as configured
    std::int64_t window_start_timestamp_ns = 0;
    std::int64_t window_end_timestamp_ns   = 0;
    // Share of the window's cognitive-load predictions at each level, in percent.
    double low_cog_load_percentage      = 0.0;
    double moderate_cog_load_percentage = 0.0;
    double high_cog_load_percentage     = 0.0;
    double mental_readiness_percentage  = 0.0;  ///< 0..100
    MentalReadinessLevel level          = MentalReadinessLevel::Low;
    int    predictions_in_window        = 0;
    double level_duration_sec           = 0.0;  ///< how long this window's level has held
    std::string level_label;                    ///< level's name, from the SDK: "Low" or "High"
};

/// Mental readiness over every configured report window, delivered with each
/// new cognitive-load prediction once the session is long enough (see
/// TheiaSDK::set_mental_readiness_min_session_seconds()).
struct MentalReadinessWindowsResult {
    std::vector<MentalReadinessWindowMeasurement> windows;  ///< one per report window, in the order set
    std::int64_t timestamp_ns = 0;
};

// -- Level changes ----------------------------------------------------------
//
// One event when a session's first level is known, then one each time the
// level changes. previous_level_label and new_level_label are the two levels'
// names, from the SDK.

struct CogLoadTransitionEvent {
    CogLoadLevel     previous_level = CogLoadLevel::Low;
    CogLoadLevel     new_level      = CogLoadLevel::Low;
    TransitionReason reason         = TransitionReason::SessionInitial;
    std::int64_t     timestamp_ns   = 0;
    double           previous_level_duration_sec = 0.0;  ///< how long previous_level lasted; 0 when session-initial
    std::string      previous_level_label;
    std::string      new_level_label;
};

struct FatigueTransitionEvent {
    FatigueLevel     previous_level = FatigueLevel::Alert;
    FatigueLevel     new_level      = FatigueLevel::Alert;
    TransitionReason reason         = TransitionReason::SessionInitial;
    std::int64_t     timestamp_ns   = 0;
    double           previous_level_duration_sec = 0.0;  ///< how long previous_level lasted; 0 when session-initial
    std::string      previous_level_label;
    std::string      new_level_label;
};

struct AttentionTransitionEvent {
    AttentionLevel   previous_level = AttentionLevel::Neither;
    AttentionLevel   new_level      = AttentionLevel::Neither;
    TransitionReason reason         = TransitionReason::SessionInitial;
    std::int64_t     timestamp_ns   = 0;
    double           previous_level_duration_sec = 0.0;  ///< how long previous_level lasted; 0 when session-initial
    std::string      previous_level_label;
    std::string      new_level_label;
};

/// Tracked per report window, so each window has its own session-initial
/// event and its own changes.
struct MentalReadinessTransitionEvent {
    double               nominal_window_seconds       = kWholeSessionWindow;
    double               actual_window_length_seconds = 0.0;  ///< shorter than nominal early in a session
    MentalReadinessLevel previous_level               = MentalReadinessLevel::Low;
    MentalReadinessLevel new_level                    = MentalReadinessLevel::Low;
    TransitionReason     reason                       = TransitionReason::SessionInitial;
    double               mental_readiness_percentage  = 0.0;  ///< the window's readiness that produced new_level
    std::int64_t         timestamp_ns                 = 0;
    double               previous_level_duration_sec  = 0.0;  ///< how long previous_level lasted; 0 when session-initial
    std::string          previous_level_label;
    std::string          new_level_label;
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
    Stasis,         ///< A Stasis head-mounted eye camera, whose gaze you push; see GazeSample
    VarjoXR3,       ///< A Varjo XR-3 headset, whose gaze you push; see GazeSample
    Quest,          ///< Meta Quest Pro or Quest 3 eye tracking, whose gaze you push
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
///
/// Device::Stasis reads timestamp_ns, the tracked eye's optical axis and its
/// eye status, plus its pupil diameter if you have one. The optical axis is
/// the camera's gaze vector as it reports it: +x right, +y DOWN, +z forward;
/// the SDK converts it. Set the eye status to 2 (open) when the camera found
/// the pupil and 0 (lost) when it didn't: blinks are read from those losses,
/// or from gaps in gaze if you leave the status alone.
///
/// Device::VarjoXR3 reads timestamp_ns, the optical axes, the per-eye
/// validity, the per-eye pupil diameters and the head quaternion. Push every
/// sample the headset reports (200 Hz). Each optical axis is that eye's gaze
/// direction in the head's frame (+x right, +y up, +z forward); set an eye's
/// validity to 0 when the headset reports it invalid, and a pupil diameter to
/// 0 when there is none. The quaternion is Hamilton (w, x, y, z), in Varjo's
/// right-handed world. Leave it at its default when the head pose is not valid.
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

    // Gaze direction in degrees, for trackers that report it as angles. None
    // of the devices above reads these; leave them unset.
    double gaze_yaw_deg   = NON_COMPUTE_VALUE;
    double gaze_pitch_deg = NON_COMPUTE_VALUE;
    // The tracker's own eyes-closed flag: 1 closed, 0 open, -1 not provided.
    std::int8_t eyes_closed = -1;

    // MediaPipe FaceLandmarker eyelid geometry in raw image pixels, for the
    // PARTICIPANT's left/right eye (webcam / Tobii-Nexus only). Send the three
    // per eye and `face_present`; the SDK converts them to an aperture in
    // millimetres itself. Leave them unset on every other tracker.
    //
    //   eyelid_gap   upper-to-lower lid distance
    //   iris_width   horizontal iris diameter (the pixels-to-mm reference)
    //   eye_width    outer-to-inner eye corner distance
    double eyelid_gap_left_px   = NON_COMPUTE_VALUE;
    double eyelid_gap_right_px  = NON_COMPUTE_VALUE;
    double iris_width_left_px   = NON_COMPUTE_VALUE;
    double iris_width_right_px  = NON_COMPUTE_VALUE;
    double eye_width_left_px    = NON_COMPUTE_VALUE;
    double eye_width_right_px   = NON_COMPUTE_VALUE;

    // Head pose (webcam / Tobii-Nexus only): the head's rotation matrix in
    // the camera frame (Rrc = row r, column c) and its x/y translation in
    // centimetres. `head_pose_tz`, above, is the z translation. Leave them
    // unset when there is no face.
    double head_pose_R00 = NON_COMPUTE_VALUE;
    double head_pose_R10 = NON_COMPUTE_VALUE;
    double head_pose_R20 = NON_COMPUTE_VALUE;
    double head_pose_R01 = NON_COMPUTE_VALUE;
    double head_pose_R11 = NON_COMPUTE_VALUE;
    double head_pose_R21 = NON_COMPUTE_VALUE;
    double head_pose_R02 = NON_COMPUTE_VALUE;
    double head_pose_R12 = NON_COMPUTE_VALUE;
    double head_pose_R22 = NON_COMPUTE_VALUE;
    double head_pose_tx  = NON_COMPUTE_VALUE;
    double head_pose_ty  = NON_COMPUTE_VALUE;

    // Per-frame flags (webcam / Tobii-Nexus only): 1 or 0, unset when there
    // is no value. `face_present`: a face was found in
    // this frame. `blink_detected`: the eyes are blinking in this frame; where
    // it is set, the SDK takes its blinks from it instead of from missing gaze.
    double face_present   = NON_COMPUTE_VALUE;
    double blink_detected = NON_COMPUTE_VALUE;
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
    /// $THEIA_LICENSE_KEY. Not needed with lease_file or an API key.
    std::string lease_token;
    std::string lease_expected_device_id;  ///< optional: require the lease to name this device
    std::string lease_expected_tenant_id;  ///< optional: require the lease to name this tenant

    /// Where this machine keeps its lease. The SDK starts from it (or from
    /// lease_token, whichever is newer) and replaces it, readable by you
    /// only, each time it renews or obtains a lease. Empty uses
    /// $THEIA_LEASE_FILE, then, with an API key set, .theia/lease in your
    /// home directory ($HOME, or %USERPROFILE% on Windows).
    std::string lease_file;
    /// Your API key. With it, a machine that has no lease, or whose lease
    /// expired while it was offline, obtains one by itself. Empty uses
    /// $THEIA_API_KEY_ID and $THEIA_API_KEY_SECRET.
    std::string lease_api_key_id;
    std::string lease_api_key_secret;
    /// This machine's name when it obtains a lease. Empty uses
    /// machine_device_identifier(). Set it (to a serial number, say) where
    /// machines are cloned from one disk image and may share an OS machine
    /// ID, and in containers, which usually have none.
    std::string lease_device_identifier;
    /// The SDK renews its lease in the background before it is due to
    /// expire. Set this to turn that off; renew_lease() still works.
    bool disable_lease_auto_renew = false;

    bool enable_cog_load_model = true;
    bool enable_fatigue_model  = true;

    /// Where downloaded models are cached. Empty uses $THEIA_MODEL_CACHE_DIR,
    /// then ~/.theia/models.
    std::string model_cache_dir;

    /// The SDK's control server, which lets tools on this machine drive the
    /// SDK over JSON-RPC (HTTPS), runs unless turned off. This is the path to
    /// its JSON config. Left empty, it runs with its defaults and accepts only
    /// the SDK's default token, which reaches the connection, status and
    /// readiness-settings methods and nothing about the user. A config with
    /// "enabled": false turns it off.
    std::string rpc_config_path;
    /// Turn the control server off, whatever rpc_config_path says.
    bool disable_rpc_server = false;

    std::function<void(const std::string&)>   on_log;
    std::function<void(const CogLoadResult&)> on_cog_load;
    std::function<void(const FatigueResult&)> on_fatigue;
    std::function<void(const AttentionResult&)>              on_attention;
    std::function<void(const MentalReadinessWindowsResult&)> on_mental_readiness;
    std::function<void(const CogLoadTransitionEvent&)>         on_cog_load_transition;
    std::function<void(const FatigueTransitionEvent&)>         on_fatigue_transition;
    std::function<void(const AttentionTransitionEvent&)>       on_attention_transition;
    std::function<void(const MentalReadinessTransitionEvent&)> on_mental_readiness_transition;
    /// Each lease the SDK renews or obtains, as it does. Only needed to keep
    /// the lease somewhere other than lease_file.
    std::function<void(const std::string&)>  on_lease_renewed;
    /// Eye strain (Device::Screen and Device::Stasis), from inside
    /// add_gaze_samples() each time it scores a second: about once a second.
    std::function<void(const EyeStrainResult&)> on_eye_strain;
};

/// Settings for one TheiaSDK::subscribe_* subscription. Left empty, every
/// event goes to the callbacks and nowhere else.
struct SubscriptionOptions {
    /// Which events reach the subscription. Empty passes every event.
    ///
    /// A subset of CloudEvents SQL: =, != (or <>), <, >, <=, >=, IN (...) and
    /// NOT IN (...), with the field on the left; AND, OR, NOT and
    /// parentheses; numbers, and strings in single or double quotes. Fields
    /// are the event's own, as named here (for mental readiness, those of a
    /// MentalReadinessWindowMeasurement, plus timestamp_ns), except the
    /// cognitive-load and fatigue `label`. Level changes add `direction`:
    /// 'increase', 'decrease', or 'unchanged' when session-initial. `reason`
    /// is 'session-initial' or 'changed'. Level fields (level, previous_level,
    /// new_level) are the level's value and compare in order: 0 Low <
    /// 1 Moderate < 2 High (cognitive load), 0 Alert < 1 Somewhat Fatigued <
    /// 2 Fatigued, 1 Narrow < 2 ModerateNarrow < 3 Neither < 4 ModerateBroad <
    /// 5 Broad, and 0 Low < 1 High (mental readiness). Each has a
    /// <field>_label (level_label, previous_level_label, new_level_label),
    /// the level's name, which takes =, !=, IN and NOT IN only and must be
    /// one of those names. An event lacking a field the filter reads never
    /// matches.
    ///
    /// Mental readiness: a filter that does not name nominal_window_seconds
    /// tests only the shortest report window; naming it tests the windows it
    /// selects, whose values must equal configured windows exactly.
    ///
    ///     "new_level_label = 'High' AND direction = 'increase'"
    ///     "direction = 'decrease' AND previous_level_duration_sec >= 600"
    ///     "level_label = 'High' AND level_duration_sec > 300"
    ///     "level >= 1"
    ///     "level_label IN ('Somewhat Fatigued', 'Fatigued')"
    std::string filter;
    /// Also POST each event the filter passes here, as JSON. Must be https
    /// (http only to this machine). Sent from a queue on a thread of its own;
    /// failures go to Options::on_log and are not retried.
    std::string webhook_url;
    /// Extra request headers for the webhook, one "Name: value" each.
    std::vector<std::string> webhook_headers;
};

/// The lease the SDK holds, and how keeping it current is going. Times are
/// Unix seconds.
struct LeaseInfo {
    bool         has_lease = false;
    std::string  device_id;
    std::string  tenant_id;
    std::string  lease_id;
    std::int64_t issued_at     = 0;
    std::int64_t refresh_after = 0;  ///< renewal is due from here
    std::int64_t warn_after    = 0;  ///< worth telling someone from here, if it has not renewed
    std::int64_t expires_at    = 0;  ///< the SDK will not start with this lease after it
    bool         auto_renew   = false;
    bool         can_activate = false;  ///< an API key is set and HarmonEyes has not refused it
    std::string  lease_file;
    std::int64_t last_renewal_attempt_unix_ms = 0;  ///< 0 = none yet
    std::string  last_renewal_error;                ///< empty if the last attempt succeeded
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

    /// Use this gaze source. Device::Screen, Device::Headset, Device::Stasis,
    /// Device::VarjoXR3 and Device::Quest need either this or
    /// add_gaze_samples(). It cannot be changed while running.
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
    /// call, in time order. For Device::Screen, Device::Headset,
    /// Device::Stasis, Device::VarjoXR3 and Device::Quest, in place of
    /// set_tracker() and start_realtime_data(): start a session, then call
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
    // get_attention_style(), get_mental_readiness() and get_eye_strain()
    // report the running state and return the same value until it changes.

    [[nodiscard]] std::optional<CogLoadResult>   get_cog_load() const;
    [[nodiscard]] std::optional<FatigueResult>   get_fatigue_level() const;
    [[nodiscard]] std::optional<AttentionResult> get_attention_style() const;

    /// Nothing before the session's first scored second, and nothing at all
    /// on a device other than Device::Screen and Device::Stasis.
    [[nodiscard]] std::optional<EyeStrainResult> get_eye_strain() const;

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

    /// Where a report window's mental readiness turns from Low to High, as a
    /// fraction in [0, 1] (default 0.5), not a percentage. A value outside
    /// that range is rejected and logged through Options::on_log, keeping the
    /// previous threshold.
    void set_mental_readiness_low_high_threshold(double threshold);

    /// The windows mental readiness is reported over, in seconds;
    /// kWholeSessionWindow means the whole session. The default is
    /// {kWholeSessionWindow, 3600, 60}. An invalid list is rejected and
    /// logged. Set them before start_new_session(); throws while a session is
    /// running.
    void set_mental_readiness_report_windows(const std::vector<double>& nominal_window_seconds);

    /// The report windows, in the order set.
    [[nodiscard]] std::vector<double> get_mental_readiness_report_windows() const;

    // -- Event subscriptions --
    //
    // Each subscribe_* adds callbacks for one kind of event, beside the
    // Options callback for it, with their own filter and optional webhook.
    // Any number of subscriptions per event. The callbacks run on the SDK's
    // thread after the Options callback, in list order; do no blocking work
    // in them. Each returns the subscription's id, never 0. An invalid filter
    // or webhook URL throws std::invalid_argument and subscribes nothing.

    std::uint64_t subscribe_cog_load(std::vector<std::function<void(const CogLoadResult&)>> callbacks,
                                     const SubscriptionOptions& options = {});
    std::uint64_t subscribe_fatigue(std::vector<std::function<void(const FatigueResult&)>> callbacks,
                                    const SubscriptionOptions& options = {});
    std::uint64_t subscribe_attention(std::vector<std::function<void(const AttentionResult&)>> callbacks,
                                      const SubscriptionOptions& options = {});
    std::uint64_t subscribe_mental_readiness(
        std::vector<std::function<void(const MentalReadinessWindowsResult&)>> callbacks,
        const SubscriptionOptions& options = {});
    std::uint64_t subscribe_cog_load_transition(
        std::vector<std::function<void(const CogLoadTransitionEvent&)>> callbacks,
        const SubscriptionOptions& options = {});
    std::uint64_t subscribe_fatigue_transition(
        std::vector<std::function<void(const FatigueTransitionEvent&)>> callbacks,
        const SubscriptionOptions& options = {});
    std::uint64_t subscribe_attention_transition(
        std::vector<std::function<void(const AttentionTransitionEvent&)>> callbacks,
        const SubscriptionOptions& options = {});
    std::uint64_t subscribe_mental_readiness_transition(
        std::vector<std::function<void(const MentalReadinessTransitionEvent&)>> callbacks,
        const SubscriptionOptions& options = {});

    /// Remove a subscription. False for an id that is unknown or already
    /// removed. Returns once any call of its callbacks already under way has
    /// finished, so what they use may be released straight after, except
    /// when called from inside one of them.
    bool unsubscribe(std::uint64_t subscription_id);

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

    /// Renew the lease now rather than when it is due. Throws
    /// LeaseRenewalError if HarmonEyes refuses or cannot be reached, keeping
    /// the current lease.
    void renew_lease();

    [[nodiscard]] LeaseInfo lease_info() const;

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
