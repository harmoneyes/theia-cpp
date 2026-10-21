// HarmonEyes Theia SDK Example: Webcam
//
// Uses your webcam to estimate gaze and prints real-time cognitive load,
// fatigue, attention and mental readiness, saving them to results/.
//
// Prerequisites:
//   1. A device lease in THEIA_LEASE (contact support@harmoneyes.com if you
//      don't have one).
//   2. Node.js 20+ on your PATH (the webcam gaze engine runs on Node).
//   3. A connected webcam.
//
// Usage:
//   export THEIA_LEASE=...
//   ./theia-webcam-streaming

#include <harmoneyes/theia.hpp>
#include <harmoneyes/theia_webcam.hpp>

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

namespace het = harmoneyes::theia;

// Fatigue updates every ~120 s, so 400 s captures at least three updates.
constexpr std::chrono::seconds kCollectionDuration{400};

namespace {

volatile std::sig_atomic_t g_interrupted = 0;
void on_interrupt(int) { g_interrupted = 1; }

std::string new_session_id()
{
    std::random_device rd;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) { out << std::setw(8) << rd(); }
    return out.str();
}

}  // namespace

int main()
{
    std::signal(SIGINT, on_interrupt);

    try {
        het::TheiaSDK sdk{het::Device::Webcam};

        // The webcam gaze source. Starting the data stream (below) opens the
        // camera and starts the gaze engine.
        het::NexusWebcamTracker webcam;
        sdk.set_tracker(webcam);

        const std::string session_id = new_session_id();
        std::cout << "[TheiaSDK] Starting session " << session_id << '\n';
        sdk.start_new_session(session_id);

        std::cout << "[TheiaSDK] Starting webcam + gaze engine\n";
        sdk.start_realtime_data();

        std::filesystem::create_directories("results");
        const std::string results_path = "results/webcam_session_" + session_id.substr(0, 8) + ".csv";
        std::ofstream     results{results_path};
        results << "elapsed_s,cog_load,cog_load_label,fatigue,fatigue_label,"
                   "attention_level,attention_label\n";

        std::cout << "Collecting data for " << kCollectionDuration.count()
                  << "s -- Ctrl+C to stop early\n\n";

        const auto start = std::chrono::steady_clock::now();
        while (!g_interrupted && std::chrono::steady_clock::now() - start < kCollectionDuration) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            results << std::fixed << std::setprecision(2) << elapsed;
            std::ostringstream status;
            status << std::fixed << std::setprecision(1);

            if (auto cog = sdk.get_cog_load()) {
                results << ',' << static_cast<int>(cog->level) << ',' << cog->label;
                status << "  CogLoad=" << cog->label;
            } else {
                results << ",,";
            }

            if (auto fatigue = sdk.get_fatigue_level()) {
                results << ',' << static_cast<int>(fatigue->level) << ',' << fatigue->label;
                status << "  Fatigue=" << fatigue->label;
            } else {
                results << ",,";
            }

            if (auto attention = sdk.get_attention_style()) {
                results << ',' << static_cast<int>(attention->level) << ',' << attention->label;
                status << "  Attention=" << attention->label;
            } else {
                results << ",,";
            }

            if (auto readiness = sdk.get_mental_readiness(elapsed)) {
                status << "  Readiness: lo=" << readiness->low_percentage
                       << "% mod=" << readiness->moderate_percentage
                       << "% hi=" << readiness->high_percentage << '%';
            }

            results << '\n';
            const std::string line = status.str();
            std::cout << '[' << std::fixed << std::setprecision(1) << std::setw(5) << elapsed << "s]"
                      << (line.empty() ? "  (warming up...)" : line) << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        if (g_interrupted) { std::cout << "\n[TheiaSDK] Interrupted by user\n"; }
        std::cout << "[TheiaSDK] Stopping session\n";
        sdk.stop_processing();
        std::cout << "[TheiaSDK] Results saved to " << results_path << '\n';
    } catch (const het::LeaseVerificationError& e) {
        std::cerr << "[TheiaSDK] Lease rejected: " << e.what() << "\n"
                  << "Set THEIA_LEASE to a valid device lease "
                     "(contact support@harmoneyes.com if you don't have one).\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[TheiaSDK] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
