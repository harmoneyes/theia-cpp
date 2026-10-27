// HarmonEyes Theia SDK Example: Pupil Labs Neon
//
// Connects to a Pupil Labs Neon eye tracker, streams gaze data, and prints
// one line a second with every result (cognitive load, fatigue, attention
// style and mental readiness), saving cognitive load and fatigue to results/.
//
// Prerequisites:
//   1. Your HarmonEyes API key in THEIA_API_KEY_ID and THEIA_API_KEY_SECRET
//      (see the README, "Device Lease").
//   2. The Pupil Labs Neon paired and reachable on the same network.
//
// Usage:
//   export THEIA_API_KEY_ID=...
//   export THEIA_API_KEY_SECRET=...
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

/// Mental readiness is High while the session has mostly not been at high
/// cognitive load (its low and moderate shares together reach half), Low
/// otherwise.
const char* readiness_state(const het::MentalReadinessResult& readiness)
{
    return readiness.low_percentage + readiness.moderate_percentage >= 50.0 ? "High" : "Low";
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

        // get_cog_load() and get_fatigue_level() hand over each result once, so
        // keep the newest for the printed line; attention style and mental
        // readiness are running states, read on the spot.
        std::string cog_load = "warming up";
        std::string fatigue  = "not yet";
        const auto start = std::chrono::steady_clock::now();
        while (!g_interrupted && std::chrono::steady_clock::now() - start < kCollectionDuration) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            results << std::fixed << std::setprecision(2) << elapsed;

            // Cognitive load: updates about once a second after warmup.
            if (auto cog = sdk.get_cog_load()) {
                results << ',' << static_cast<int>(cog->level) << ',' << cog->label;
                cog_load = cog->label;
            } else {
                results << ",,";
            }

            // Fatigue: updates about every 120 seconds.
            if (auto level = sdk.get_fatigue_level()) {
                results << ',' << static_cast<int>(level->level) << ',' << level->label;
                fatigue = level->label;
            } else {
                results << ",,";
            }

            results << '\n';

            const auto attention = sdk.get_attention_style();
            const auto readiness = sdk.get_mental_readiness();
            std::cout << "  Cognitive load: " << cog_load << " | Fatigue: " << fatigue
                      << " | Attention: " << (attention ? attention->label : "not yet")
                      << " | Mental readiness: " << (readiness ? readiness_state(*readiness) : "not yet") << '\n';
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        if (g_interrupted) { std::cout << "\n[TheiaSDK] Interrupted by user\n"; }
        std::cout << "[TheiaSDK] Stopping session\n";
        sdk.stop_processing();
        std::cout << "[TheiaSDK] Results saved to " << results_path << '\n';
    } catch (const het::LeaseVerificationError& e) {
        std::cerr << "[TheiaSDK] Lease rejected: " << e.what() << "\n"
                  << "Set THEIA_API_KEY_ID and THEIA_API_KEY_SECRET to your HarmonEyes API key "
                     "(see the README, \"Device Lease\").\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[TheiaSDK] " << e.what() << '\n';
        return 1;
    }
    return 0;
}
