// HarmonEyes Theia SDK Example: Webcam (Tobii Nexus)
//
// Streams gaze from a webcam and prints cognitive load, fatigue, attention and
// mental readiness once a second. WebcamSource spawns a Node sidecar hosting
// the Tobii Nexus engine, and a TrackerPump feeds its samples into the SDK.
//
// Prerequisites:
//   1. export THEIA_LEASE=...               device lease
//   2. export THEIA_TOBII_LICENSE_FILE=...  Tobii engine licence
//      (contact sales@harmoneyes.com if you don't have either)
//   3. Node.js 20+ on your PATH, and a connected webcam
//
// Set the screen geometry below to your actual display.
//
// Usage:
//   ./theia-webcam-streaming

#include <harmoneyes/theia/errors.hpp>
#include <harmoneyes/theia/nexus_webcam.hpp>
#include <harmoneyes/theia/sdk.hpp>
#include <harmoneyes/theia/tracker.hpp>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

namespace het  = harmoneyes::theia;
namespace hetn = harmoneyes::theia::nexus;

constexpr double kCollectionSeconds = 400.0;

namespace {

std::string lease_from_env()
{
    if (const char* v = std::getenv("THEIA_LEASE")) { return v; }
    if (const char* v = std::getenv("THEIA_LICENSE_KEY")) { return v; }
    return {};
}

std::string session_id()
{
    std::random_device rd;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) { out << std::setw(8) << rd(); }
    return out.str();
}

const char* cog_load_label(het::CogLoadLevel level)
{
    switch (level) {
        case het::CogLoadLevel::Low:      return "Low";
        case het::CogLoadLevel::Moderate: return "Moderate";
        case het::CogLoadLevel::High:     return "High";
    }
    return "Unknown";
}

const char* fatigue_label(het::SleepinessLevel level)
{
    switch (level) {
        case het::SleepinessLevel::Alert:            return "Alert";
        case het::SleepinessLevel::SomewhatFatigued: return "Somewhat Fatigued";
        case het::SleepinessLevel::Fatigued:         return "Fatigued";
    }
    return "Unknown";
}

std::string fixed2(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::ofstream open_results(const std::string& name, const std::string& header, std::string& path)
{
    std::error_code ec;
    std::filesystem::create_directories("results", ec);

    const std::time_t now = std::time(nullptr);
    std::tm           tm{};
#ifdef _WIN32
    ::localtime_s(&tm, &now);
#else
    ::localtime_r(&now, &tm);
#endif
    std::ostringstream stamp;
    stamp << std::put_time(&tm, "%Y%m%d_%H%M%S");

    path = "results/" + name + "_" + stamp.str() + ".csv";
    std::ofstream out{path};
    if (out) { out << header << '\n'; }
    return out;
}

volatile std::sig_atomic_t g_interrupted = 0;
void on_sigint(int) { g_interrupted = 1; }

}  // namespace

int main()
{
    std::signal(SIGINT, on_sigint);

    het::Options options;
    options.lease_token   = lease_from_env();
    options.config.device = het::DeviceType::Nexus;

    std::unique_ptr<het::TheiaSDK> sdk;
    try {
        sdk = std::make_unique<het::TheiaSDK>(std::move(options));
    } catch (const het::LeaseVerificationError& e) {
        std::cerr << "[TheiaSDK] Lease rejected: " << e.what() << "\n"
                  << "Set THEIA_LEASE to a valid device lease "
                     "(contact sales@harmoneyes.com if you don't have one).\n";
        return 1;
    } catch (const het::Error& e) {
        std::cerr << "[TheiaSDK] Could not start: " << e.what() << '\n';
        return 1;
    }

    hetn::WebcamSource::Config camera;
    camera.frame_width          = 640;
    camera.frame_height         = 480;
    camera.fps                  = 30;
    camera.screen_width_mm      = 336;
    camera.screen_height_mm     = 210;
    camera.screen_res_width_px  = 1920;
    camera.screen_res_height_px = 1080;

    try {
        camera.license = hetn::TobiiLicense::from_env();
    } catch (const std::exception& e) {
        std::cerr << "[TheiaSDK] " << e.what() << '\n';
        return 1;
    }

    std::string   results_path;
    std::ofstream results = open_results(
        "webcam_session",
        "elapsed_s,cog_load,cog_load_label,fatigue,fatigue_label,attention_level,attention_label",
        results_path);
    std::size_t rows = 0;

    hetn::WebcamSource source{std::move(camera)};
    try {
        std::cout << "[TheiaSDK] Opening the camera and starting the gaze sidecar\n";
        source.start();
    } catch (const std::exception& e) {
        std::cerr << "[TheiaSDK] Webcam source failed: " << e.what() << '\n';
        return 1;
    }

    try {
        const std::string id = session_id();
        std::cout << "[TheiaSDK] Starting session " << id << '\n';
        auto session = sdk->start_session(id);

        het::TrackerPump::Options pump_options;
        pump_options.on_stall = [](double idle_seconds) {
            if (idle_seconds > 0.0) {
                std::cerr << "[TheiaSDK] No samples for " << idle_seconds << "s\n";
            }
        };
        pump_options.on_error = [](const std::exception& e) {
            std::cerr << "[TheiaSDK] Pump error: " << e.what() << '\n';
        };

        het::TrackerPump pump{*sdk, source, std::move(pump_options)};
        pump.start();

        std::cout << "Collecting for " << kCollectionSeconds << "s -- Ctrl+C to stop early\n\n";

        const auto start = std::chrono::steady_clock::now();
        while (!g_interrupted) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= kCollectionSeconds) { break; }

            std::string row = fixed2(elapsed);
            std::string status;

            if (const auto cog = sdk->cog_load()) {
                row += "," + std::to_string(static_cast<int>(cog->level)) + "," +
                       cog_load_label(cog->level);
                status += "CogLoad=";
                status += cog_load_label(cog->level);
            } else {
                row += ",,";
            }

            if (const auto fatigue = sdk->fatigue()) {
                row += "," + std::to_string(static_cast<int>(fatigue->level)) + "," +
                       fatigue_label(fatigue->level);
                if (!status.empty()) { status += "  |  "; }
                status += "Fatigue=";
                status += fatigue_label(fatigue->level);
            } else {
                row += ",,";
            }

            if (const auto attention = sdk->attention()) {
                row += "," + std::to_string(static_cast<int>(attention->level)) + "," +
                       attention->label;
                if (!status.empty()) { status += "  |  "; }
                status += "Attention=" + attention->label;
            } else {
                row += ",,";
            }

            if (const auto readiness = sdk->mental_readiness(elapsed)) {
                if (!status.empty()) { status += "  |  "; }
                status += "Readiness lo=" + fixed2(readiness->low_percentage) +
                          "% mod=" + fixed2(readiness->moderate_percentage) +
                          "% hi=" + fixed2(readiness->high_percentage) + "%";
            }

            results << row << '\n' << std::flush;
            ++rows;
            std::cout << '[' << fixed2(elapsed) << "s] "
                      << (status.empty() ? "(warming up...)" : status) << '\n'
                      << std::flush;

            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        if (g_interrupted) { std::cout << "\n[TheiaSDK] Interrupted\n"; }
        std::cout << "[TheiaSDK] Stopping\n";
        pump.stop();
        session.stop();
    } catch (const std::exception& e) {
        std::cerr << "[TheiaSDK] " << e.what() << '\n';
        source.close();
        return 1;
    }

    source.close();

    if (results) {
        std::cout << "[TheiaSDK] Results saved to " << results_path << " (" << rows << " rows)\n";
    }
    return 0;
}
