// HarmonEyes Theia SDK Example: Tobii Pro Glasses 3
//
// Streams gaze from a pair of Tobii Pro Glasses 3 and prints cognitive load
// and fatigue once a second. The gaze rate and calibration are both set before
// the session starts. The address is optional: the G3 advertises itself over
// mDNS.
//
// Prerequisites:
//   export THEIA_LEASE=...   (contact sales@harmoneyes.com if you don't have one)
//
// Usage:
//   ./theia-tobii-g3 [ip]

#include <harmoneyes/theia/errors.hpp>
#include <harmoneyes/theia/sdk.hpp>

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

namespace het = harmoneyes::theia;

// 50 or 100. Use 50: see set_tobii_g3_gaze_frequency() for why 100 skews the
// legacy features.
constexpr int kGazeHz = 50;

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

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_sigint);

    const std::string ip = (argc > 1) ? argv[1] : std::string{};

    het::Options options;
    options.lease_token   = lease_from_env();
    options.config.device = het::DeviceType::TobiiG3;

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

    std::string   results_path;
    std::ofstream results = open_results(
        "tobii_g3_session", "elapsed_s,cog_load,cog_load_label,fatigue,fatigue_label",
        results_path);
    std::size_t rows = 0;

    try {
        sdk->set_tobii_g3_connection(ip);

        if (!sdk->set_tobii_g3_gaze_frequency(kGazeHz)) {
            std::cerr << "[TheiaSDK] Device refused " << kGazeHz << " Hz\n";
            return 1;
        }
        if (!sdk->calibrate_tobii_g3()) {
            std::cerr << "[TheiaSDK] Calibration failed. Reseat the glasses and retry.\n";
            return 1;
        }

        const std::string id = session_id();
        std::cout << "[TheiaSDK] Starting session " << id << '\n';
        auto session = sdk->start_session(id);

        std::cout << "[TheiaSDK] Starting data stream\n";
        sdk->start_data_streaming();

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

            results << row << '\n' << std::flush;
            ++rows;
            std::cout << '[' << fixed2(elapsed) << "s] "
                      << (status.empty() ? "(warming up...)" : status) << '\n'
                      << std::flush;

            std::this_thread::sleep_for(std::chrono::seconds{1});
        }

        if (g_interrupted) { std::cout << "\n[TheiaSDK] Interrupted\n"; }
        std::cout << "[TheiaSDK] Stopping session\n";
        session.stop();
    } catch (const het::Error& e) {
        std::cerr << "[TheiaSDK] " << e.what() << '\n';
        return 1;
    }

    if (results) {
        std::cout << "[TheiaSDK] Results saved to " << results_path << " (" << rows << " rows)\n";
    }
    return 0;
}
