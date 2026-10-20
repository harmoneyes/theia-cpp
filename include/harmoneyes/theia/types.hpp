#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "harmoneyes/theia/detail/c_api.hpp"

namespace harmoneyes::theia {

/// The value the pipeline uses for "this could not be computed".
///
/// A real number, not a NaN, so it compares equal and must be tested for
/// explicitly. Note that unset screen resolution uses -1 instead, because
/// those fields are integers; see GazeSample.
inline constexpr double NON_COMPUTE_VALUE = -8888.0;

// ── Levels ───────────────────────────────────────────────────────────────

/// Cognitive load band.
enum class CogLoadLevel : int { Low = 0, Moderate = 1, High = 2 };

/// Fatigue band, from the KSS scale.
///
/// Named SleepinessLevel to match the Python SDK. The native library calls the
/// same thing FatigueLevel, aliased below.
enum class SleepinessLevel : int {
    Alert            = 0,  // KSS 1-4
    SomewhatFatigued = 1,  // KSS 5-6
    Fatigued         = 2,  // KSS 7-9
};

/// The native library's name for SleepinessLevel.
using FatigueLevel = SleepinessLevel;

/// Attentional style, from narrow focus to broad scanning.
///
/// Numbered from 1, not 0. The C ABI reports this as a bare int, so treating
/// it as zero-based shifts the whole scale by one band.
enum class AttentionLevel : int {
    Narrow         = 1,
    ModerateNarrow = 2,
    Neither        = 3,
    ModerateBroad  = 4,
    Broad          = 5,
};

/// Mental readiness band.
enum class MentalReadinessLevel : int { Low = 0, High = 1 };

/// Why a level-transition event fired.
///
/// On SessionInitial the previous and new levels are equal: it reports the
/// starting level rather than a change.
enum class TransitionReason : int { SessionInitial = 0, Changed = 1 };

/// Classification of a single gaze sample.
///
/// Provided for reading recorded classification output. Nothing in this SDK
/// returns one: the C ABI has no entry point that produces a classification.
enum class EyeMovementType : std::uint8_t {
    Unknown  = 0,
    Fixation = 1,
    Saccade  = 2,
    Blink    = 3,
};

/// Which tracker the pipeline is configured for.
enum class DeviceType {
    PupilLabsNeon,  ///< Pupil Labs Neon, 200 Hz, discovered over mDNS
    Ganzin,         ///< Ganzin Sol, connected by IP
    Nexus,          ///< Webcam, 30 Hz. Samples are pushed in; see nexus.hpp
    Mock,           ///< No device. Samples are pushed in; skips model download
    TobiiG3,        ///< Tobii Pro Glasses 3, recorded exports
};

/// The platform string the native library expects for a device.
///
/// The library matches these by string and falls back to Pupil Labs for
/// anything it does not recognise, so a typo is silently accepted rather than
/// rejected. That is why callers pick a DeviceType instead of passing a string.
[[nodiscard]] inline const char* platform_string(DeviceType device) noexcept
{
    switch (device) {
        case DeviceType::Ganzin:  return "Ganzin";
        case DeviceType::Nexus:   return "Nexus";
        case DeviceType::Mock:    return "Mock";
        case DeviceType::TobiiG3: return "TobiiG3";
        case DeviceType::PupilLabsNeon:
            break;
    }
    return "PL";
}

// ── Samples ──────────────────────────────────────────────────────────────

/// One gaze sample.
///
/// Aliases the C ABI struct. The library copies every field on the way in
/// regardless, so this is not about avoiding a copy: it is that the C struct
/// already has exactly the right 52 fields, and wrapping it would mean
/// maintaining a 52-field converter for no gain.
///
/// **Start from make_gaze_sample(), not from `GazeSample s{}`.** Zeroing is
/// not a neutral starting point: several fields have non-zero "absent" values
/// and the library does not reapply them. In particular `blink_id` means "no
/// blink" at -1, so a zeroed sample claims to be part of blink 0 and the
/// feature pipeline discards it.
///
/// Fields left at zero that the library DOES rewrite to a sentinel on the way
/// in, as all-or-nothing groups:
///   - the ten IMU fields (accel, gyro, quaternion)   -> NON_COMPUTE_VALUE
///   - eyelid_aperture_left_mm / _right_mm            -> NON_COMPUTE_VALUE
///   - gaze_x_norm / gaze_y_norm                      -> NON_COMPUTE_VALUE
///   - screen_width_mm / screen_height_mm             -> NON_COMPUTE_VALUE
///   - screen_res_width_px / screen_res_height_px     -> -1
///   - head_pose_depth_cm / head_pose_tz              -> NON_COMPUTE_VALUE
///
/// Because the rewrite keys on the whole group being zero, filling only part
/// of a group leaves the rest as literal zeros. And a genuinely measured
/// normalised gaze of exactly (0.0, 0.0), the top-left pixel, is
/// indistinguishable from unset and will be discarded.
using GazeSample = ::gaze_sample_t;

// Catch a vendored-header change that silently alters the meaning of every
// field. Update these only together with a review of the field order.
static_assert(std::is_standard_layout<GazeSample>::value,
              "GazeSample must stay standard-layout");
static_assert(sizeof(GazeSample) == 224,
              "vendored gaze_sample_t changed size; re-verify every field");
static_assert(offsetof(GazeSample, blink_id) == 64,
              "vendored gaze_sample_t field order changed");
static_assert(offsetof(GazeSample, head_pose_tz) == 216,
              "vendored gaze_sample_t field order changed");

/// A sample with the library's real "absent" defaults applied.
///
/// This is the only safe starting point. See GazeSample for why.
[[nodiscard]] inline GazeSample make_gaze_sample() noexcept
{
    GazeSample s{};
    s.left_eye_validity         = 1;      // 0 would mean invalid
    s.right_eye_validity        = 1;
    s.left_eye_status           = 2;      // 0 = lost, 1 = blink, 2 = open
    s.right_eye_status          = 2;
    s.left_eye_confidence       = 1.0F;
    s.right_eye_confidence      = 1.0F;
    s.blink_id                  = -1;     // -1 = not part of a blink
    s.eyelid_angle_top_left     = -8888.0F;
    s.eyelid_angle_bottom_left  = -8888.0F;
    s.eyelid_angle_top_right    = -8888.0F;
    s.eyelid_angle_bottom_right = -8888.0F;
    return s;
}

// ── Configuration ────────────────────────────────────────────────────────

/// Which tuned preset the pipeline runs.
///
/// Selecting a device applies that tracker's full configuration: sample rate,
/// pixels per degree, blink-detection method and roughly fifty other
/// parameters.
///
/// The C ABI also accepts six raw numbers (sample rate, window size, screen
/// size, dpi, viewing distance) INSTEAD of a preset, and this SDK deliberately
/// does not expose them. They cannot express px_per_deg or the
/// blink-detection method, so supplying them does not mean "the preset with
/// six overrides", it means running that tracker's data through the Pupil Labs
/// pipeline. For Pupil Labs itself the two are identical; for every other
/// tracker it silently produces wrong features.
///
/// Per-session screen geometry belongs on each GazeSample
/// (screen_width_mm, screen_res_width_px and friends), not here.
struct Config {
    DeviceType device = DeviceType::PupilLabsNeon;
};

}  // namespace harmoneyes::theia
