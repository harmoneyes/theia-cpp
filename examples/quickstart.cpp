// HarmonEyes Theia SDK: the smallest complete program.
//
// Streams from a Pupil Labs Neon for a minute and prints cognitive load and
// fatigue as they arrive.
//
// Usage:
//   export THEIA_LEASE=...   (contact support@harmoneyes.com if you don't have one)
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
        het::TheiaSDK sdk{het::Device::PupilLabsNeon};   // reads THEIA_LEASE

        sdk.start_new_session("quickstart-" + std::to_string(std::time(nullptr)));
        sdk.start_realtime_data();

        for (int second = 0; second < 60; ++second) {
            if (auto cog = sdk.get_cog_load()) {
                std::cout << "Cognitive load: " << cog->label << '\n';
            }
            if (auto fatigue = sdk.get_fatigue_level()) {
                std::cout << "Fatigue: " << fatigue->label << '\n';
            }
            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        sdk.stop_processing();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
