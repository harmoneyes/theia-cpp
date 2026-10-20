// DefaultTracker reads files written by the SDK itself, which means it must
// cope with a Windows-written file and a non-English locale. Both failures are
// silent: wrong numbers, no error.

#include <harmoneyes/theia/replay.hpp>
#include "check.hpp"

#include <clocale>
#include <filesystem>
#include <fstream>

namespace het = harmoneyes::theia;
using hen_test::check;

int main()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "hen_replay_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        std::ofstream f{dir / "sdk_input_000.csv", std::ios::binary};
        f << "timestamp,Blink_ID,leftEyeX,leftEyeY,eyelid_angle_top_left,PupilDiameter\n"
          << "1763140928903088511,7,1.5,2.5,0.25,3.5\n";
    }
    {   // Written on Windows: every line ends CRLF.
        std::ofstream f{dir / "sdk_input_001.csv", std::ios::binary};
        f << "timestamp,Blink_ID,leftEyeX\r\n1763140928903088512,9,4.5\r\n";
    }
    {   // A session directory also holds this; it is not gaze input.
        std::ofstream f{dir / "imu_000.csv", std::ios::binary};
        f << "timestamp\n1\n";
    }

    // A locale where the decimal separator is a comma. std::stod would read
    // "1.5" as 1.0 here, silently.
    std::setlocale(LC_ALL, "de_DE.UTF-8");

    het::DefaultTracker::Options options;
    options.input_dir         = dir;
    options.simulate_realtime = false;

    het::DefaultTracker tracker{options};
    tracker.load();
    check(tracker.size() == 2, "sdk_input_*.csv loaded, imu_*.csv ignored");

    const auto samples = tracker.get_buffered_data();
    if (samples.size() < 2) { check(false, "two samples available"); return hen_test::report("replay"); }

    check(samples[0].timestamp_ns == 1763140928903088511LL,
          "nanosecond timestamp exact (not routed through a double)");
    check(samples[0].left_eye_x == 1.5F, "decimal parsed despite a comma-decimal locale");
    check(samples[0].left_eye_y == 2.5F, "second decimal too");
    check(samples[0].eyelid_angle_top_left == 0.25F, "eyelid angle column read");
    check(samples[0].blink_id == 7, "blink id read");
    check(samples[1].left_eye_x == 4.5F, "last column of a CRLF file read");
    check(samples[1].blink_id == 9, "CRLF header names matched");
    check(tracker.exhausted(), "everything handed out");

    fs::remove_all(dir);
    return hen_test::report("replay");
}
