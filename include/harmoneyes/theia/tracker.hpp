#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <functional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "harmoneyes/theia/sdk.hpp"
#include "harmoneyes/theia/types.hpp"

namespace harmoneyes::theia {

/// A source of gaze samples the SDK does not drive itself.
///
/// For Pupil Labs, Ganzin and Tobii G3 the library connects to the device and
/// you never need this: call TheiaSDK::start_data_streaming() instead. Webcam
/// and replay sources push samples in from outside, and this is the interface
/// they implement.
///
/// Implementations are polled from a single thread, so they do not need to be
/// thread-safe against themselves, but get_buffered_data() must not block for
/// long.
class EyeTracker {
public:
    virtual ~EyeTracker() = default;

    EyeTracker()                             = default;
    EyeTracker(const EyeTracker&)            = default;
    EyeTracker& operator=(const EyeTracker&) = default;
    EyeTracker(EyeTracker&&)                 = default;
    EyeTracker& operator=(EyeTracker&&)      = default;

    /// Samples accumulated since the last call. Returning empty is normal.
    ///
    /// Build each sample with make_gaze_sample(), not `GazeSample{}`: see
    /// types.hpp for why a zeroed sample is discarded by the pipeline.
    [[nodiscard]] virtual std::vector<GazeSample> get_buffered_data() = 0;

    /// Whether the underlying device is currently usable.
    [[nodiscard]] virtual bool is_connected() const = 0;

    /// Whether samples are actively arriving. Defaults to is_connected().
    [[nodiscard]] virtual bool is_streaming() const { return is_connected(); }

    /// Release the device. Called by the owner, not by TrackerPump.
    virtual void close() {}
};

/// Drains an EyeTracker into a TheiaSDK on a background thread.
///
/// Deliberately a separate object rather than something TheiaSDK starts for
/// you, so the thread's lifetime is visible in the code and ends at a scope
/// you chose. Construction order is the contract: both the SDK and the tracker
/// must outlive the pump.
///
/// The SDK must also not be MOVED while a pump is running. TheiaSDK is
/// move-only, so moving it is the natural idiom, but the pump holds a
/// reference: after a move that reference names a moved-from object and every
/// feed throws, routed to on_error.
///
/// start() and stop() are safe to call from different threads. A callback may
/// call stop() to wind the pump down; it will request the stop and return
/// rather than joining itself. A callback must not destroy the pump.
///
/// Non-copyable and non-movable, because the running thread captures `this`.
class TrackerPump {
public:
    struct Options {
        /// How often to drain the tracker.
        std::chrono::milliseconds poll_interval{10};

        /// Report a stall after this long with no samples. Zero disables it.
        std::chrono::milliseconds stall_after{5000};

        /// Called once per stall, with the seconds since the last sample, and
        /// again when samples resume (with 0.0). Runs on the pump's thread.
        std::function<void(double)> on_stall;

        /// Called when draining or feeding throws. Runs on the pump's thread.
        /// Without it, errors are swallowed and the pump keeps going.
        std::function<void(const std::exception&)> on_error;
    };

    TrackerPump(TheiaSDK& sdk, EyeTracker& tracker, Options options)
        : sdk_(sdk), tracker_(tracker), options_(std::move(options)) {}

    // Not a defaulted argument: Options is a nested type, and its default
    // member initializers are not available inside the enclosing class while
    // that class is still incomplete.
    TrackerPump(TheiaSDK& sdk, EyeTracker& tracker);

    ~TrackerPump() { stop(); }

    TrackerPump(const TrackerPump&)            = delete;
    TrackerPump& operator=(const TrackerPump&) = delete;
    TrackerPump(TrackerPump&&)                 = delete;
    TrackerPump& operator=(TrackerPump&&)      = delete;

    /// Start draining. Throws if already running.
    void start()
    {
        const std::lock_guard<std::mutex> lock(lifecycle_);
        if (running_.exchange(true)) {
            throw std::logic_error("TrackerPump::start: already running");
        }
        try {
            thread_ = std::thread([this] { run(); });
        } catch (...) {
            // Thread creation can fail on resource exhaustion. Without this
            // the pump would claim to be running with no thread behind it.
            running_ = false;
            throw;
        }
    }

    /// Stop and join. Safe to call more than once, and safe if never started.
    void stop() noexcept
    {
        // Called from the pump's own thread, via a callback: request the stop
        // and return. Joining here would be joining ourselves, which throws
        // EDEADLK, and throwing from a noexcept function terminates.
        if (worker_id_.load() == std::this_thread::get_id()) {
            running_ = false;
            return;
        }

        const std::lock_guard<std::mutex> lock(lifecycle_);
        running_ = false;
        if (thread_.joinable()) { thread_.join(); }
        worker_id_ = std::thread::id{};
    }

    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

private:
    void run() noexcept
    {
        worker_id_ = std::this_thread::get_id();
        using clock = std::chrono::steady_clock;
        auto last_sample = clock::now();
        bool stalled     = false;

        while (running_.load()) {
            try {
                std::vector<GazeSample> batch = tracker_.get_buffered_data();
                if (!batch.empty()) {
                    sdk_.add_gaze_samples(batch);
                    last_sample = clock::now();
                    if (stalled) {
                        stalled = false;
                        if (options_.on_stall) { options_.on_stall(0.0); }
                    }
                } else if (!stalled && options_.stall_after.count() > 0) {
                    const auto idle = clock::now() - last_sample;
                    if (idle >= options_.stall_after) {
                        stalled = true;
                        if (options_.on_stall) {
                            options_.on_stall(
                                std::chrono::duration<double>(idle).count());
                        }
                    }
                }
            } catch (const std::exception& e) {
                // The thread must not let anything escape: that would call
                // std::terminate and take the host down.
                if (options_.on_error) {
                    try { options_.on_error(e); } catch (...) {}
                }
            } catch (...) {
                // Same reasoning, for anything not derived from std::exception.
            }

            std::this_thread::sleep_for(options_.poll_interval);
        }
    }

    TheiaSDK&                     sdk_;
    EyeTracker&                   tracker_;
    Options                       options_;
    std::atomic<bool>             running_{false};
    std::atomic<std::thread::id>  worker_id_{};
    std::mutex                    lifecycle_;  ///< serialises start() and stop()
    std::thread                   thread_;
};

inline TrackerPump::TrackerPump(TheiaSDK& sdk, EyeTracker& tracker)
    : TrackerPump(sdk, tracker, Options{}) {}

}  // namespace harmoneyes::theia
