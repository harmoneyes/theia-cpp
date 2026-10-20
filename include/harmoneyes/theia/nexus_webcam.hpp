#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "harmoneyes/theia/detail/sidecar_protocol.hpp"
#include "harmoneyes/theia/detail/subprocess.hpp"
#include "harmoneyes/theia/nexus.hpp"
#include "harmoneyes/theia/tracker.hpp"

namespace harmoneyes::theia::nexus {

/// The licence the Tobii Nexus engine needs.
///
/// Separate from the HarmonEyes device lease: this one authorises the gaze
/// engine, the lease authorises the SDK. The engine licence is issued as
/// UTF-16LE bytes, which is why decoding is part of this type rather than
/// something callers are left to work out.
///
/// **It must be a SIGNED licence, not the issued `.bytes` file.** The engine
/// uses two-tier signing: the issued file carries Tobii's `--SE-SIGNATURE--`
/// over terms valid for a year or more, and a signing service must append a
/// short-lived `--LICENSEE-BODY--` plus `--LICENSEE-SIGNATURE--` per session.
/// Handing the engine the issued file alone fails with
/// `LICENSE_VALIDATION_RESULT_TAMPERED`, which reads like a corrupt file but
/// means the second signature is simply absent.
///
/// Fetching it over HTTP is deliberately not built in: a header-only SDK
/// should not oblige every consumer to link an HTTP stack. Get a signed
/// licence from your signing endpoint and hand the bytes over.
class TobiiLicense {
public:
    TobiiLicense() = default;

    /// From an already-decoded string.
    [[nodiscard]] static TobiiLicense from_string(std::string text)
    {
        TobiiLicense l;
        l.text_ = std::move(text);
        return l;
    }

    /// From UTF-16LE bytes, as the signing endpoint returns them.
    [[nodiscard]] static TobiiLicense from_utf16le(const std::vector<unsigned char>& bytes)
    {
        if (bytes.size() % 2 != 0) {
            throw std::invalid_argument("TobiiLicense: UTF-16LE input has an odd byte count");
        }
        std::string out;
        out.reserve(bytes.size() / 2);
        std::size_t i = 0;
        if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) { i = 2; }  // BOM
        for (; i + 1 < bytes.size(); i += 2) {
            if (bytes[i + 1] != 0) {
                throw std::invalid_argument(
                    "TobiiLicense: input is not Basic Latin UTF-16LE; refusing to mangle it");
            }
            if (bytes[i] != 0) { out += static_cast<char>(bytes[i]); }
        }
        // Trim surrounding whitespace the endpoint may include.
        const auto first = out.find_first_not_of(" \t\r\n");
        const auto last  = out.find_last_not_of(" \t\r\n");
        TobiiLicense l;
        l.text_ = (first == std::string::npos) ? std::string{} : out.substr(first, last - first + 1);
        return l;
    }

    /// From a file of UTF-16LE bytes.
    [[nodiscard]] static TobiiLicense from_file(const std::filesystem::path& path)
    {
        std::ifstream in{path, std::ios::binary};
        if (!in) { throw std::runtime_error("TobiiLicense: cannot read " + path.string()); }
        return from_utf16le({std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}});
    }

    /// From $THEIA_TOBII_LICENSE, or the file named by $THEIA_TOBII_LICENSE_FILE.
    [[nodiscard]] static TobiiLicense from_env()
    {
        if (const char* text = std::getenv("THEIA_TOBII_LICENSE")) { return from_string(text); }
        if (const char* file = std::getenv("THEIA_TOBII_LICENSE_FILE")) { return from_file(file); }
        throw std::runtime_error(
            "TobiiLicense: set THEIA_TOBII_LICENSE or THEIA_TOBII_LICENSE_FILE, "
            "or pass a licence explicitly");
    }

    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] bool empty() const noexcept { return text_.empty(); }

private:
    std::string text_;
};

/// Webcam gaze, by hosting the Tobii Nexus engine in a Node sidecar.
///
/// The native library does not estimate gaze from a camera: it consumes samples
/// someone else produced. The engine that does the estimating is a WASM module
/// with no C++ binding, hosted by a small Node process that streams gaze back
/// as newline-delimited JSON. This class owns that process.
///
/// It therefore needs **Node 20 or newer on PATH**, the sidecar bundle, and a
/// Tobii engine licence. An air-gapped machine cannot obtain the licence, so
/// webcam tracking is unavailable there.
///
/// The sidecar refuses an init that does not name the directory holding its
/// WASM, so Config::tobii_package_dir must resolve: either set it, or leave it
/// empty and have the bundle under sidecar_dir in `tobii/` or
/// `node_modules/tobii_nexus_web/dist/`. Both are checked before launching.
///
/// Implements EyeTracker, so it is driven by a TrackerPump like any other
/// pushed source.
class WebcamSource : public EyeTracker {
public:
    struct Config {
        /// Camera frame size requested from the sidecar.
        int frame_width  = 640;
        int frame_height = 480;
        int fps          = 30;

        /// Screen geometry, passed to the engine and attached to every sample.
        int    screen_width_mm  = 336;
        int    screen_height_mm = 210;
        double fov_deg          = 75.0;

        /// Resolution used to turn normalised gaze into pixels.
        int screen_res_width_px  = 1920;
        int screen_res_height_px = 1080;

        /// Which camera, when the machine has several. Empty means the default.
        std::string device_id;

        /// The engine licence. Required unless launch_command is overridden.
        TobiiLicense license;

        /// Directory holding sidecar.mjs. Empty uses $THEIA_WEBCAM_SIDECAR_DIR.
        std::filesystem::path sidecar_dir;

        /// Directory holding nexus_wasm.wasm. Empty derives it from
        /// sidecar_dir, probing `tobii/` then
        /// `node_modules/tobii_nexus_web/dist/`, which is what the Python SDK
        /// does. The sidecar rejects an init without it.
        std::filesystem::path tobii_package_dir;

        /// Node executable. Empty uses $THEIA_NODE, then "node" from PATH.
        std::string node_path;

        /// Run something else instead of Node plus the sidecar.
        ///
        /// The seam that makes this class testable: point it at a stub that
        /// speaks the same protocol and the whole path runs with no Node, no
        /// camera and no licence.
        std::vector<std::string> launch_command;

        /// How long to wait for the sidecar to report readiness.
        std::chrono::milliseconds ready_timeout{30000};

        /// Cap on buffered samples, so a stalled consumer cannot grow without
        /// bound. Oldest are dropped.
        std::size_t max_buffered = 4096;
    };

    WebcamSource() : WebcamSource(Config{}) {}
    explicit WebcamSource(Config config)
        : config_(std::move(config)), builder_(make_builder(config_)) {}

    ~WebcamSource() override { close(); }

    WebcamSource(const WebcamSource&)            = delete;
    WebcamSource& operator=(const WebcamSource&) = delete;
    WebcamSource(WebcamSource&&)                 = delete;
    WebcamSource& operator=(WebcamSource&&)      = delete;

    /// Launch the sidecar and wait until it reports readiness.
    ///
    /// Throws with the tail of the sidecar's stderr attached, because a
    /// failure here is almost always only legible from its diagnostics.
    void start()
    {
        const std::lock_guard<std::mutex> lifecycle{lifecycle_};
        if (running_.exchange(true)) { throw std::logic_error("WebcamSource::start: already started"); }

        // A previous failed run must not be reported again.
        ready_        = false;
        failed_       = false;
        stream_ended_ = false;
        { std::lock_guard<std::mutex> lock{ready_mutex_}; failure_.clear(); }

        try {
            // Resolved before spawning, like the Python SDK, so a missing WASM
            // is a clear local error rather than a sidecar round trip.
            resolved_tobii_dir_ = resolve_tobii_dir();
            process_.start(resolve_command());
        } catch (...) {
            running_ = false;
            throw;
        }

        reader_ = std::thread([this] { read_loop(); });
        stderr_reader_ = std::thread([this] { stderr_loop(); });

        if (!process_.write_line(init_message())) {
            stop_locked();   // start() already holds lifecycle_
            throw std::runtime_error("WebcamSource: sidecar closed before init could be sent" + stderr_tail());
        }

        std::unique_lock<std::mutex> lock{ready_mutex_};
        const bool signalled = ready_cv_.wait_for(lock, config_.ready_timeout,
                                                  [this] { return ready_ || failed_; });
        if (!signalled || failed_) {
            const std::string why = failed_ ? ("sidecar reported: " + failure_) : "timed out";
            lock.unlock();
            stop_locked();   // start() already holds lifecycle_
            throw std::runtime_error("WebcamSource: not ready (" + why + ")" + stderr_tail());
        }
    }

    [[nodiscard]] std::vector<GazeSample> get_buffered_data() override
    {
        std::lock_guard<std::mutex> lock{buffer_mutex_};
        std::vector<GazeSample>     out{buffer_.begin(), buffer_.end()};
        buffer_.clear();
        return out;
    }

    /// False once the sidecar has exited, so a dead source is visible rather
    /// than reporting healthy forever while no samples arrive.
    [[nodiscard]] bool is_connected() const override
    {
        return running_.load() && !failed_.load() && !stream_ended_.load();
    }
    [[nodiscard]] bool is_streaming() const override { return ready_.load() && is_connected(); }

    void close() override { stop_internal(); }

    /// The most recent sidecar diagnostics, for logging a failure.
    [[nodiscard]] std::string stderr_tail() const
    {
        std::lock_guard<std::mutex> lock{stderr_mutex_};
        if (stderr_lines_.empty()) { return {}; }
        std::string out = "\n--- sidecar stderr ---\n";
        for (const auto& line : stderr_lines_) { out += line; out += '\n'; }
        return out;
    }

private:
    static SampleBuilder make_builder(const Config& c)
    {
        SampleBuilder::Config bc;
        bc.screen_res_width_px  = c.screen_res_width_px;
        bc.screen_res_height_px = c.screen_res_height_px;
        bc.screen_width_mm      = c.screen_width_mm;
        bc.screen_height_mm     = c.screen_height_mm;
        return SampleBuilder{bc};
    }

    /// Where nexus_wasm.wasm lives. Mirrors the Python SDK's probe order.
    [[nodiscard]] std::filesystem::path resolve_tobii_dir() const
    {
        if (!config_.tobii_package_dir.empty()) { return config_.tobii_package_dir; }

        std::filesystem::path dir = sidecar_dir();
        if (dir.empty()) {
            if (!config_.launch_command.empty()) { return {}; }   // a stub decides for itself
            throw std::runtime_error(
                "WebcamSource: no sidecar directory, so the Tobii package directory cannot "
                "be derived. Set Config::tobii_package_dir or THEIA_WEBCAM_SIDECAR_DIR.");
        }

        for (const auto& candidate : {dir / "tobii",
                                      dir / "node_modules" / "tobii_nexus_web" / "dist"}) {
            if (std::filesystem::exists(candidate / "nexus_wasm.wasm")) { return candidate; }
        }
        throw std::runtime_error(
            "WebcamSource: Tobii Nexus WASM not found under " + dir.string() +
            " (looked for tobii/ and node_modules/tobii_nexus_web/dist/)");
    }

    [[nodiscard]] std::filesystem::path sidecar_dir() const
    {
        if (!config_.sidecar_dir.empty()) { return config_.sidecar_dir; }
        const char* env = std::getenv("THEIA_WEBCAM_SIDECAR_DIR");
        return (env != nullptr) ? std::filesystem::path{env} : std::filesystem::path{};
    }

    [[nodiscard]] std::vector<std::string> resolve_command() const
    {
        if (!config_.launch_command.empty()) { return config_.launch_command; }

        std::string node = config_.node_path;
        if (node.empty()) {
            const char* env = std::getenv("THEIA_NODE");
            node = (env != nullptr) ? env : "node";
        }

        const std::filesystem::path dir = sidecar_dir();
        if (dir.empty()) {
            throw std::runtime_error(
                "WebcamSource: no sidecar directory. Set THEIA_WEBCAM_SIDECAR_DIR "
                "or Config::sidecar_dir.");
        }

        const std::filesystem::path entry = dir / "sidecar.mjs";
        if (!std::filesystem::exists(entry)) {
            throw std::runtime_error("WebcamSource: no sidecar.mjs under " + dir.string());
        }
        return {node, entry.string()};
    }

    [[nodiscard]] std::string init_message() const
    {
        namespace sc = detail::sidecar;
        std::string m = R"({"type":"init","tobiiPackageDir":)";
        sc::append_json_string(m, resolved_tobii_dir_.string());
        m += R"(,"frameWidth":)";
        m += std::to_string(config_.frame_width);
        m += R"(,"frameHeight":)" + std::to_string(config_.frame_height);
        m += R"(,"fps":)" + std::to_string(config_.fps);
        m += R"(,"screenWidthMm":)" + std::to_string(config_.screen_width_mm);
        m += R"(,"screenHeightMm":)" + std::to_string(config_.screen_height_mm);
        m += R"(,"fovDeg":)" + std::to_string(config_.fov_deg);
        m += R"(,"license":)";
        sc::append_json_string(m, config_.license.text());
        if (!config_.device_id.empty()) {
            m += R"(,"deviceId":)";
            sc::append_json_string(m, config_.device_id);
        }
        m += "}";
        return m;
    }

    void handle_line(const std::string& line)
    {
        namespace sc = detail::sidecar;
        const auto type = sc::string_field(line, "type");
        if (!type) { return; }

        if (*type == "gaze") {
            Gaze g;
            g.x            = sc::number_field(line, "x").value_or(0.0);
            g.y            = sc::number_field(line, "y").value_or(0.0);
            g.valid        = sc::bool_field(line, "valid").value_or(false);
            // The engine's own capture time, preferred over the sidecar's
            // Date.now() because the latter carries event-loop jitter that
            // would land straight in inter-sample interval and velocity. Same
            // order the Python SDK uses.
            g.timestamp_ms = sc::number_field(line, "timestamp")
                                 .value_or(sc::number_field(line, "wallTime").value_or(0.0));

            const std::size_t cap = config_.max_buffered > 0 ? config_.max_buffered : 1;
            std::lock_guard<std::mutex> lock{buffer_mutex_};
            while (buffer_.size() >= cap) { buffer_.pop_front(); }
            buffer_.push_back(builder_.build(g));
            return;
        }

        if (*type == "ready") {
            { std::lock_guard<std::mutex> lock{ready_mutex_}; ready_ = true; }
            ready_cv_.notify_all();
            return;
        }

        if (*type == "error") {
            {
                std::lock_guard<std::mutex> lock{ready_mutex_};
                failure_ = sc::string_field(line, "msg").value_or("unspecified");
                // Before readiness an error is fatal; afterwards it is a notice.
                if (!ready_) { failed_ = true; }
            }
            ready_cv_.notify_all();
        }
    }

    void read_loop() noexcept
    {
        std::string pending;
        char        chunk[4096];
        while (running_.load()) {
            const std::size_t got = process_.read_stdout(chunk, sizeof(chunk));
            if (got == 0) { break; }   // end of stream
            pending.append(chunk, got);

            std::size_t start = 0;
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (pending[i] != '\n') { continue; }
                try { handle_line(pending.substr(start, i - start)); } catch (...) {}
                start = i + 1;
            }
            pending.erase(0, start);
            // A line that never ends is a protocol fault, not a reason to grow
            // without bound.
            if (pending.size() > 1u << 16) { pending.clear(); }
        }
        // EOF means the sidecar is gone, whether or not it ever got ready.
        stream_ended_ = true;
        {
            std::lock_guard<std::mutex> lock{ready_mutex_};
            if (!ready_) {
                failed_ = true;
                if (failure_.empty()) { failure_ = "exited before reporting readiness"; }
            }
        }
        ready_cv_.notify_all();
    }

    void stderr_loop() noexcept
    {
        std::string pending;
        char        chunk[1024];
        while (running_.load()) {
            const std::size_t got = process_.read_stderr(chunk, sizeof(chunk));
            if (got == 0) { break; }
            pending.append(chunk, got);
            std::size_t start = 0;
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (pending[i] != '\n') { continue; }
                std::lock_guard<std::mutex> lock{stderr_mutex_};
                if (stderr_lines_.size() >= 40) { stderr_lines_.pop_front(); }
                stderr_lines_.push_back(pending.substr(start, i - start));
                start = i + 1;
            }
            pending.erase(0, start);
            if (pending.size() > 1u << 16) { pending.clear(); }
        }
    }

    /// Serialised, so a close() concurrent with the destructor cannot return
    /// early and let the threads outlive the object.
    void stop_internal() noexcept
    {
        const std::lock_guard<std::mutex> lifecycle{lifecycle_};
        stop_locked();
    }

    /// The shutdown itself. The caller must already hold lifecycle_.
    void stop_locked() noexcept
    {
        if (!running_.exchange(false)) { return; }

        if (process_.running()) {
            process_.write_line(R"({"type":"destroy"})");
            process_.close_stdin();
            if (!process_.wait_for_exit(std::chrono::milliseconds{2000})) { process_.terminate(); }
        }
        process_.terminate();   // reap, and guarantee the write ends are gone

        // Child death is what gives the readers EOF. Closing our own read end
        // here would not wake a thread already blocked in read(), and would
        // free the descriptor number for reuse while that thread still holds
        // it, so the pipes are closed only after both readers have joined.
        if (reader_.joinable())        { reader_.join(); }
        if (stderr_reader_.joinable()) { stderr_reader_.join(); }
        process_.close_pipes();
    }

    Config            config_;
    SampleBuilder     builder_;
    detail::Process   process_;
    std::thread       reader_;
    std::thread       stderr_reader_;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> stream_ended_{false};  ///< the sidecar's stdout reached EOF
    std::mutex        lifecycle_;            ///< serialises start() and close()
    std::filesystem::path resolved_tobii_dir_;

    mutable std::mutex      buffer_mutex_;
    std::deque<GazeSample>  buffer_;

    std::mutex              ready_mutex_;
    std::condition_variable ready_cv_;
    std::string             failure_;

    mutable std::mutex      stderr_mutex_;
    std::deque<std::string> stderr_lines_;
};

}  // namespace harmoneyes::theia::nexus
