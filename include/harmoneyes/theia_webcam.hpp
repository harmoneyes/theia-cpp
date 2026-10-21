#pragma once

/// Webcam support for the HarmonEyes Theia SDK. Include this alongside
/// <harmoneyes/theia.hpp> when you use Device::Webcam.

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <harmoneyes/theia.hpp>

namespace harmoneyes::theia {

/// Webcam gaze, estimated by the bundled Tobii Nexus engine.
///
/// Use it with Device::Webcam:
///
///     TheiaSDK sdk{Device::Webcam};
///     NexusWebcamTracker webcam;
///     sdk.set_tracker(webcam);
///     sdk.start_new_session(session_uuid);
///     sdk.start_realtime_data();   // opens the camera
///
/// Needs **Node.js 20 or newer** on PATH: the engine runs in a small Node
/// process that ships with this SDK (vendor/webcam-sidecar). When you ship
/// your application, copy that directory alongside it and point
/// Config::sidecar_dir at the copy.
class NexusWebcamTracker : public EyeTracker {
public:
    struct Config {
        /// Camera frame size and rate.
        int width  = 640;
        int height = 480;
        int fps    = 30;

        /// Physical screen size and camera field of view. Set these to your
        /// display for the best accuracy.
        int    screen_width_mm  = 336;
        int    screen_height_mm = 210;
        double fov_deg          = 75.0;

        /// Which camera, on a machine with several. Empty uses the default.
        std::string device_id;

        /// Where to fetch the gaze engine's licence. Empty uses
        /// $TOBII_LICENSE_URL, then the address shipped with the SDK.
        std::string license_url;

        /// The webcam-sidecar directory. Empty uses $THEIA_WEBCAM_SIDECAR_DIR,
        /// then the copy in this repository.
        std::filesystem::path sidecar_dir;

        /// The Node executable. Empty uses $THEIA_NODE, then `node` on PATH.
        std::string node_path;

        /// How long to wait for the camera and engine to come up.
        std::chrono::milliseconds ready_timeout{30000};
    };

    NexusWebcamTracker();
    explicit NexusWebcamTracker(Config config);
    ~NexusWebcamTracker() override;

    NexusWebcamTracker(const NexusWebcamTracker&)            = delete;
    NexusWebcamTracker& operator=(const NexusWebcamTracker&) = delete;
    NexusWebcamTracker(NexusWebcamTracker&&)                 = delete;
    NexusWebcamTracker& operator=(NexusWebcamTracker&&)      = delete;

    /// Open the camera and wait until the engine reports it is ready.
    /// TheiaSDK::start_realtime_data() calls this for you.
    void start() override;

    [[nodiscard]] std::vector<GazeSample> get_buffered_data() override;

    /// False once the webcam process has exited.
    [[nodiscard]] bool is_connected() const override;

    /// Release the camera. Safe to call more than once.
    void close() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace harmoneyes::theia

#define HARMONEYES_THEIA_INCLUDING_WEBCAM_IMPL
#include "harmoneyes/theia/detail/webcam_impl.hpp"
#undef HARMONEYES_THEIA_INCLUDING_WEBCAM_IMPL
