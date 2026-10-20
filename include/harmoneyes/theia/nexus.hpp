#pragma once

#include <cmath>
#include <cstdint>

#include "harmoneyes/theia/types.hpp"

namespace harmoneyes::theia::nexus {

/// One gaze reading from a webcam gaze engine.
///
/// Coordinates are normalised to [0, 1] with the origin at the top-left of the
/// screen, which is what the Tobii Nexus engine reports.
struct Gaze {
    double x         = 0.0;
    double y         = 0.0;
    bool   valid     = false;
    double timestamp_ms = 0.0;  ///< milliseconds since the Unix epoch
};

/// Turns webcam gaze into the sample shape the Nexus pipeline expects.
///
/// This is the piece that decides whether webcam features are meaningful, and
/// it is easy to get wrong in a way that looks fine. The feature pipeline
/// computes every angular feature from `gaze_x_norm` / `gaze_y_norm`
/// (ace/src/devices/device_models/nexus_device.cpp). If only pixel coordinates
/// are filled, a fallback still marks the sample valid, so the stream looks
/// healthy while azimuth and elevation come out NaN and every angular feature
/// degrades to the non-compute sentinel.
///
/// Both of the gaze producers that existed before this SDK omit those fields.
/// Setting them is the single most important thing this class does.
class SampleBuilder {
public:
    struct Config {
        /// Screen resolution, used to turn normalised gaze into pixels.
        int screen_res_width_px  = 1920;
        int screen_res_height_px = 1080;

        /// Physical screen size. Supplied together with the resolution or not
        /// at all: the pipeline treats the geometry as all-or-nothing.
        int screen_width_mm  = 336;
        int screen_height_mm = 210;

        /// Attach the screen geometry to every sample. Turn this off only if
        /// the geometry is genuinely unknown.
        bool include_screen_geometry = true;
    };

    SampleBuilder() = default;
    explicit SampleBuilder(Config config) noexcept : config_(config) {}

    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /// Build one sample.
    [[nodiscard]] GazeSample build(const Gaze& gaze) const noexcept
    {
        GazeSample s = make_gaze_sample();

        s.timestamp_ns = static_cast<std::int64_t>(gaze.timestamp_ms * 1.0e6);

        if (gaze.valid) {
            // The pipeline reads angles from these two. Without them the
            // sample still counts as valid and every angular feature is lost.
            s.gaze_x_norm = nudge_off_zero(static_cast<float>(gaze.x));
            s.gaze_y_norm = nudge_off_zero(static_cast<float>(gaze.y));

            const auto px = static_cast<float>(gaze.x * config_.screen_res_width_px);
            const auto py = static_cast<float>(gaze.y * config_.screen_res_height_px);
            s.left_eye_x = s.right_eye_x = px;
            s.left_eye_y = s.right_eye_y = py;

            s.left_eye_validity = s.right_eye_validity = 1;
            s.left_eye_status   = s.right_eye_status   = 2;  // open
        } else {
            // Leave the gaze fields at their "absent" values and mark the eyes
            // closed, which is how the Nexus blink detector sees a lost frame.
            s.left_eye_x = s.right_eye_x = std::nanf("");
            s.left_eye_y = s.right_eye_y = std::nanf("");
            s.left_eye_validity = s.right_eye_validity = 0;
            s.left_eye_status   = s.right_eye_status   = 1;  // blink / closed
        }

        // A webcam reports no pupil size.
        s.pupil_diameter_left  = NON_COMPUTE_VALUE;
        s.pupil_diameter_right = NON_COMPUTE_VALUE;
        s.pupil_diameter       = NON_COMPUTE_VALUE;

        if (config_.include_screen_geometry) {
            s.screen_width_mm      = static_cast<float>(config_.screen_width_mm);
            s.screen_height_mm     = static_cast<float>(config_.screen_height_mm);
            s.screen_res_width_px  = config_.screen_res_width_px;
            s.screen_res_height_px = config_.screen_res_height_px;
        }

        return s;
    }

private:
    /// Keep a real measurement distinguishable from "unset".
    ///
    /// The library rewrites gaze_x_norm and gaze_y_norm to the non-compute
    /// sentinel when both are exactly zero, so a genuine reading at the
    /// top-left corner would be thrown away. One ULP is far below the
    /// engine's precision and costs nothing.
    [[nodiscard]] static float nudge_off_zero(float value) noexcept
    {
        return value == 0.0F ? std::nextafter(0.0F, 1.0F) : value;
    }

    Config config_{};
};

}  // namespace harmoneyes::theia::nexus
