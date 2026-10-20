// HarmonEyes Theia SDK Example: scoring a recording
//
// Replays a recorded session through the same pipeline a live session uses and
// collects every cognitive-load and fatigue prediction.
//
// There is no batch call to wrap: Python's predict_cog_load_batch() is
// implemented in Python on top of pandas. Here you push the samples in and
// gather the callbacks. Predictions are taken by callback rather than polling,
// because polling clears the slot and would drop windows.
//
// Input: a directory of sdk_input_*.csv files. The `timestamp` column is
// milliseconds, which is what the SDK writes.
//
// Prerequisites:
//   export THEIA_LEASE=...   (contact sales@harmoneyes.com if you don't have one)
//
// Usage:
//   ./theia-pupil-labs-batch path/to/recording_dir

#include <harmoneyes/theia/errors.hpp>
#include <harmoneyes/theia/replay.hpp>
#include <harmoneyes/theia/sdk.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace het = harmoneyes::theia;

constexpr std::size_t kChunk       = 200;
constexpr auto        kQuietPeriod = std::chrono::seconds{3};
constexpr auto        kDrainCap    = std::chrono::seconds{30};

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


struct Collected {
    std::mutex                      mutex;
    std::vector<het::CogLoadResult> cog_load;
    std::vector<het::FatigueResult> fatigue;

    std::size_t count()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        return cog_load.size() + fatigue.size();
    }
};

}  // namespace

int main(int argc, char** argv)
{
    // Declared before the SDK so it outlives the callbacks that reference it.
    Collected collected;

    het::Options options;
    options.lease_token   = lease_from_env();
    options.config.device = het::DeviceType::PupilLabsNeon;

    options.on_cog_load = [&collected](const het::CogLoadResult& r) {
        const std::lock_guard<std::mutex> lock(collected.mutex);
        collected.cog_load.push_back(r);
    };
    options.on_fatigue = [&collected](const het::FatigueResult& r) {
        const std::lock_guard<std::mutex> lock(collected.mutex);
        collected.fatigue.push_back(r);
    };

    het::DefaultTracker::Options replay;
    if (argc > 1) { replay.input_dir = argv[1]; }
    replay.simulate_realtime = false;

    std::vector<het::GazeSample> samples;
    try {
        het::DefaultTracker tracker{replay};
        tracker.load();
        samples = tracker.get_buffered_data();
    } catch (const std::exception& e) {
        std::cerr << "[TheiaSDK] Could not read the recording: " << e.what() << '\n'
                  << "Pass a directory of sdk_input_*.csv files, or set SDK_INPUT_DIR.\n";
        return 1;
    }

    if (samples.empty()) {
        std::cerr << "[TheiaSDK] The recording contains no samples.\n";
        return 1;
    }
    std::cout << "[TheiaSDK] Loaded " << samples.size() << " samples\n";

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

    try {
        auto session = sdk->start_session(session_id());

        for (std::size_t offset = 0; offset < samples.size(); offset += kChunk) {
            const std::size_t take = std::min(kChunk, samples.size() - offset);
            sdk->add_gaze_samples(samples.data() + offset, take);

            const std::size_t done = offset + take;
            if (done % (kChunk * 50) == 0 || done == samples.size()) {
                std::cout << "  fed " << done << '/' << samples.size() << " samples, "
                          << collected.count() << " predictions so far\n"
                          << std::flush;
            }
        }

        std::cout << "[TheiaSDK] All samples fed; waiting for the pipeline to drain\n";
        const auto  deadline = std::chrono::steady_clock::now() + kDrainCap;
        auto        last     = std::chrono::steady_clock::now();
        std::size_t seen     = collected.count();
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            const std::size_t now_seen = collected.count();
            if (now_seen != seen) {
                seen = now_seen;
                last = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - last > kQuietPeriod) {
                break;
            }
        }

        session.stop();
    } catch (const het::Error& e) {
        std::cerr << "[TheiaSDK] " << e.what() << '\n';
        return 1;
    }

    std::vector<het::CogLoadResult> cog;
    std::vector<het::FatigueResult> fatigue;
    {
        const std::lock_guard<std::mutex> lock(collected.mutex);
        cog     = collected.cog_load;
        fatigue = collected.fatigue;
    }

    std::cout << "\nCognitive load: " << cog.size() << " predictions\n";
    if (cog.empty()) {
        std::cout << "  None. The recording is probably shorter than the ~30s warmup.\n";
    } else {
        const std::int64_t origin = cog.front().timestamp_ns;

        std::string   results_path;
        std::ofstream results =
            open_results("batch_cog_load", "offset_s,level,label,confidence", results_path);
        for (const auto& r : cog) {
            const double offset = static_cast<double>(r.timestamp_ns - origin) / 1e9;
            results << fixed2(offset) << ',' << static_cast<int>(r.level) << ','
                    << cog_load_label(r.level) << ',' << r.confidence << '\n';
        }

        std::vector<std::size_t> picks{0, cog.size() / 2, cog.size() - 1};
        picks.erase(std::unique(picks.begin(), picks.end()), picks.end());
        for (const std::size_t i : picks) {
            const double offset = static_cast<double>(cog[i].timestamp_ns - origin) / 1e9;
            std::cout << "  t=" << fixed2(offset) << "s  " << cog_load_label(cog[i].level)
                      << "  conf=" << cog[i].confidence << '\n';
        }
        if (results) { std::cout << "  written to " << results_path << '\n'; }
    }

    std::cout << "\nFatigue: " << fatigue.size() << " predictions\n";
    if (fatigue.empty()) {
        std::cout << "  None. Fatigue needs roughly 120s of data after the warmup.\n";
    } else {
        for (const auto& r : fatigue) {
            std::cout << "  batch " << r.batch_number << "  " << fatigue_label(r.level)
                      << "  conf=" << r.confidence << '\n';
        }
    }

    return 0;
}
