// HarmonEyes Theia SDK Example: Pupil Labs Neon
//
// Connects to a Pupil Labs Neon eye tracker, streams gaze data, and prints
// real-time cognitive load and fatigue predictions, saving them to results/.
//
// Prerequisites:
//   1. A device lease in THEIA_LEASE (contact support@harmoneyes.com if you
//      don't have one).
//   2. The Pupil Labs Neon paired and reachable on the same network.
//
// Usage:
//   export THEIA_LEASE=...
//   ./theia-pupil-labs-streaming

#include <harmoneyes/theia.hpp>

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
        het::TheiaSDK sdk{het::Device::PupilLabsNeon};

        const std::string session_id = new_session_id();
        std::cout << "[TheiaSDK] Starting session " << session_id << '\n';
        sdk.start_new_session(session_id);

        std::cout << "[TheiaSDK] Starting data stream\n";
        sdk.start_realtime_data();

        std::filesystem::create_directories("results");
        const std::string results_path = "results/pupil_labs_session_" + session_id.substr(0, 8) + ".csv";
        std::ofstream     results{results_path};
        results << "elapsed_s,cog_load,cog_load_label,fatigue,fatigue_label\n";

        const auto start = std::chrono::steady_clock::now();
        while (!g_interrupted && std::chrono::steady_clock::now() - start < kCollectionDuration) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            results << std::fixed << std::setprecision(2) << elapsed;

            // Cognitive load: updates about once a second after warmup.
            if (auto cog = sdk.get_cog_load()) {
                results << ',' << static_cast<int>(cog->level) << ',' << cog->label;
                std::cout << "  Cognitive Load: " << cog->label << '\n';
            } else {
                results << ",,";
            }

            // Fatigue: updates about every 120 seconds.
            if (auto fatigue = sdk.get_fatigue_level()) {
                results << ',' << static_cast<int>(fatigue->level) << ',' << fatigue->label;
                std::cout << "  Fatigue: " << fatigue->label << '\n';
            } else {
                results << ",,";
            }

            results << '\n';
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
