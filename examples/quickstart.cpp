// HarmonEyes Theia SDK: the smallest complete program.
//
// Streams from a Pupil Labs Neon for a minute and prints, once a second, one
// line with every result: cognitive load, fatigue, attention style and mental
// readiness.
//
// Usage, with your HarmonEyes API key (see the README, "Device Lease"):
//   export THEIA_API_KEY_ID=...
//   export THEIA_API_KEY_SECRET=...
//   ./quickstart

#include <harmoneyes/theia.hpp>

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

namespace het = harmoneyes::theia;

int main()
{
    try {
        het::TheiaSDK sdk{het::Device::PupilLabsNeon};   // obtains or renews its lease with your API key

        sdk.start_new_session("quickstart-" + std::to_string(std::time(nullptr)));
        sdk.start_realtime_data();

        // get_cog_load() and get_fatigue_level() hand over each result once, so
        // keep the newest; attention style and mental readiness are running
        // states, read on the spot.
        std::string cog_load = "warming up";
        std::string fatigue  = "not yet";
        for (int second = 0; second < 60; ++second) {
            if (auto cog = sdk.get_cog_load()) { cog_load = cog->label; }
            if (auto level = sdk.get_fatigue_level()) { fatigue = level->label; }
            const auto attention = sdk.get_attention_style();
            const auto readiness = sdk.get_mental_readiness();
            std::cout << "Cognitive load: " << cog_load << " | Fatigue: " << fatigue
                      << " | Attention: " << (attention ? attention->label : "not yet")
                      << " | Mental readiness: "
                      << (readiness ? (readiness->low_percentage + readiness->moderate_percentage >= 50.0 ? "High"
                                                                                                          : "Low")
                                    : "not yet")
                      << '\n';
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        sdk.stop_processing();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
