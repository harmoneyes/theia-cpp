// Drives WebcamSource against a stub that enforces the REAL sidecar's contract
// (see tests/fake_sidecar.py). Needs python3 but no Node, no camera and no
// Tobii licence.
//
// The first case is the one that matters: an earlier stub accepted any init,
// so a missing required field went unnoticed and the webcam path could never
// have worked against the real sidecar.

#include <harmoneyes/theia/nexus_webcam.hpp>
#include "check.hpp"

#include <cstdlib>
#include <string>

namespace hn = harmoneyes::theia::nexus;
using hen_test::check;

static hn::WebcamSource::Config base(const std::string& stub)
{
    hn::WebcamSource::Config c;
    c.launch_command = {"python3", stub};
    c.ready_timeout  = std::chrono::milliseconds{5000};
    return c;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_webcam_lifecycle <fake_sidecar.py>\n"); return 2; }
    const std::string stub = argv[1];

    if (std::system("python3 --version > /dev/null 2>&1") != 0) {
        std::printf("python3 unavailable; skipping\n");
        return 0;
    }

    // 1. The sidecar requires tobiiPackageDir. Its absence must surface.
    {
        hn::WebcamSource cam{base(stub)};
        bool        threw = false;
        std::string what;
        try { cam.start(); } catch (const std::exception& e) { threw = true; what = e.what(); }
        check(threw, "an init the sidecar rejects fails start()");
        check(what.find("tobiiPackageDir") != std::string::npos,
              "the failure carries the sidecar's own reason");
    }

    // 2. A complete init reaches readiness and produces samples.
    {
        auto cfg = base(stub);
        cfg.tobii_package_dir = "/tmp/hen-fake-dist";
        hn::WebcamSource cam{cfg};
        cam.start();
        check(cam.is_streaming(), "streaming after a successful handshake");
        std::this_thread::sleep_for(std::chrono::milliseconds{200});
        const auto batch = cam.get_buffered_data();
        check(!batch.empty(), "samples arrive");
        if (!batch.empty()) {
            check(batch.front().gaze_x_norm == 0.25F,
                  "gaze_x_norm is set, which every angular feature depends on");
            check(batch.front().blink_id == -1, "a valid sample is not flagged as a blink");
        }
        cam.close();
        cam.close();   // idempotent
        check(!cam.is_streaming(), "closed");
    }

    // 3. A sidecar that dies must stop reporting itself as connected.
    {
        auto cfg = base(stub);
        cfg.launch_command.push_back("--die-after");
        cfg.launch_command.push_back("5");
        cfg.tobii_package_dir = "/tmp/hen-fake-dist";
        hn::WebcamSource cam{cfg};
        cam.start();
        std::this_thread::sleep_for(std::chrono::milliseconds{500});
        check(!cam.is_connected(), "a dead sidecar reports disconnected, not healthy");
    }

    // 4. A sidecar that never reports readiness times out rather than hanging.
    {
        auto cfg = base(stub);
        cfg.launch_command.push_back("--no-ready");
        cfg.tobii_package_dir = "/tmp/hen-fake-dist";
        cfg.ready_timeout     = std::chrono::milliseconds{500};
        hn::WebcamSource cam{cfg};
        bool threw = false;
        try { cam.start(); } catch (const std::exception&) { threw = true; }
        check(threw, "a sidecar that never reports ready times out");
    }

    return hen_test::report("webcam lifecycle");
}
