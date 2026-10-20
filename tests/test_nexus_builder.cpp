// The regression gate for the webcam gaze contract.
//
// Every angular feature is computed from gaze_x_norm / gaze_y_norm. Both gaze
// producers that predate this SDK set only pixel coordinates, which leaves the
// sample looking valid while azimuth and elevation come out NaN. If this test
// ever fails, webcam features are silently worthless.

#include <harmoneyes/theia/nexus.hpp>
#include "check.hpp"

namespace het = harmoneyes::theia;
namespace hn  = harmoneyes::theia::nexus;
using hen_test::check;

int main()
{
    hn::SampleBuilder builder;

    const auto s = builder.build(hn::Gaze{0.25, 0.75, true, 1700000000000.0});

    check(s.gaze_x_norm != static_cast<float>(het::NON_COMPUTE_VALUE),
          "gaze_x_norm is set, not the absent sentinel");
    check(s.gaze_y_norm != static_cast<float>(het::NON_COMPUTE_VALUE),
          "gaze_y_norm is set, not the absent sentinel");
    check(s.gaze_x_norm == 0.25F, "gaze_x_norm carries the normalised value");
    check(s.left_eye_x == 0.25F * 1920.0F, "pixels derived from the resolution");
    check(s.left_eye_y == 0.75F * 1080.0F, "pixel y derived from the resolution");
    check(s.blink_id == -1, "a valid sample is not a blink");
    check(s.left_eye_status == 2, "eyes reported open");
    check(s.pupil_diameter_left == het::NON_COMPUTE_VALUE, "a webcam reports no pupil size");
    check(s.screen_res_width_px == 1920 && s.screen_res_height_px == 1080, "resolution attached");
    check(s.screen_width_mm == 336.0F, "physical geometry attached");
    check(s.timestamp_ns == 1700000000000000000LL, "milliseconds converted to nanoseconds");

    // Exactly (0,0) is a real reading at the top-left pixel, but the library
    // rewrites an all-zero pair to the absent sentinel, so it must be nudged.
    const auto corner = builder.build(hn::Gaze{0.0, 0.0, true, 0.0});
    check(corner.gaze_x_norm != 0.0F, "a (0,0) reading is kept distinguishable from unset");
    check(corner.gaze_x_norm < 1e-30F, "the nudge is far below engine precision");

    const auto invalid = builder.build(hn::Gaze{0.5, 0.5, false, 0.0});
    check(invalid.left_eye_validity == 0, "an invalid reading marks the eyes invalid");
    check(invalid.left_eye_status == 1, "an invalid reading marks the eyes closed");

    return hen_test::report("nexus builder");
}
