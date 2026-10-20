// Guards the sample defaults. A zeroed GazeSample is silently discarded by the
// feature pipeline, so make_gaze_sample() carrying the real "absent" values is
// load-bearing rather than cosmetic.

#include <harmoneyes/theia/types.hpp>
#include "check.hpp"

#include <cstring>
#include <string>

namespace het = harmoneyes::theia;
using hen_test::check;

int main()
{
    const auto s = het::make_gaze_sample();

    check(s.blink_id == -1, "blink_id is -1 (not part of a blink)");
    check(s.left_eye_validity == 1 && s.right_eye_validity == 1, "both eyes valid");
    check(s.left_eye_status == 2 && s.right_eye_status == 2, "both eyes open");
    check(s.left_eye_confidence == 1.0F && s.right_eye_confidence == 1.0F, "full confidence");
    check(s.eyelid_angle_top_left == -8888.0F, "eyelid angles are the absent sentinel");

    // The failure this guards against: blink_id != -1 marks a blink.
    const auto zeroed = het::GazeSample{};
    check(zeroed.blink_id != -1, "a zeroed sample WOULD be treated as a blink");
    check(s.blink_id == -1, "make_gaze_sample avoids that");

    check(std::strcmp(het::platform_string(het::DeviceType::PupilLabsNeon), "PL") == 0, "PL");
    check(std::strcmp(het::platform_string(het::DeviceType::Ganzin), "Ganzin") == 0, "Ganzin");
    check(std::strcmp(het::platform_string(het::DeviceType::Nexus), "Nexus") == 0, "Nexus");
    check(std::strcmp(het::platform_string(het::DeviceType::Mock), "Mock") == 0, "Mock");
    check(std::strcmp(het::platform_string(het::DeviceType::TobiiG3), "TobiiG3") == 0, "TobiiG3");

    check(het::NON_COMPUTE_VALUE == -8888.0, "NON_COMPUTE_VALUE");
    check(static_cast<int>(het::AttentionLevel::Narrow) == 1, "AttentionLevel is 1-based");
    check(static_cast<int>(het::AttentionLevel::Broad) == 5, "AttentionLevel tops out at 5");

    return hen_test::report("types");
}
