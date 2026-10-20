#pragma once

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "harmoneyes/theia/detail/parse.hpp"
#include "harmoneyes/theia/tracker.hpp"
#include "harmoneyes/theia/types.hpp"

namespace harmoneyes::theia {

/// Replays recorded gaze from the SDK's own `sdk_input_*.csv` files.
///
/// The counterpart to the Python SDK's DefaultTracker, and the way to exercise
/// the pipeline with no hardware attached.
///
/// Columns are matched by header name, not position, and anything the header
/// does not name is left at its make_gaze_sample() default. A file whose
/// header lacks `timestamp` is rejected rather than silently replayed as
/// zeroes.
class DefaultTracker : public EyeTracker {
public:
    struct Options {
        /// Directory holding the recordings. Empty uses $SDK_INPUT_DIR, then
        /// ./sdk_input.
        std::filesystem::path input_dir;

        /// Samples per second to release when pacing. Ignored if
        /// `simulate_realtime` is false.
        int sample_rate = 200;

        /// Release samples at roughly their original rate. False returns
        /// everything on the first call, which is what tests usually want.
        bool simulate_realtime = true;
    };

    DefaultTracker() : DefaultTracker(Options{}) {}

    explicit DefaultTracker(Options options) : options_(std::move(options))
    {
        if (options_.input_dir.empty()) {
            const char* env = std::getenv("SDK_INPUT_DIR");
            options_.input_dir = (env != nullptr) ? std::filesystem::path{env}
                                                  : std::filesystem::path{"sdk_input"};
        }
    }

    /// Load every `sdk_input_*.csv` in the input directory, in name order.
    ///
    /// Throws if the directory has none: an empty replay that silently
    /// produces nothing is worse than a clear failure.
    void load()
    {
        namespace fs = std::filesystem;

        std::vector<fs::path> files;
        if (fs::is_directory(options_.input_dir)) {
            for (const auto& entry : fs::directory_iterator{options_.input_dir}) {
                const std::string name = entry.path().filename().string();
                // Deliberately not every *.csv: a session directory also holds
                // imu_*.csv and the feature output, which are not gaze input.
                if (name.rfind("sdk_input_", 0) == 0 && entry.path().extension() == ".csv") {
                    files.push_back(entry.path());
                }
            }
        }
        // Name order is chronological: the writer rolls over with a counter.
        std::sort(files.begin(), files.end());

        if (files.empty()) {
            throw std::runtime_error("DefaultTracker: no sdk_input_*.csv in " +
                                     options_.input_dir.string());
        }

        samples_.clear();
        for (const auto& file : files) { read_file(file); }
        cursor_  = 0;
        started_ = false;
    }

    /// Samples due since the last call.
    [[nodiscard]] std::vector<GazeSample> get_buffered_data() override
    {
        if (samples_.empty() || cursor_ >= samples_.size()) { return {}; }

        std::size_t take = samples_.size() - cursor_;
        if (options_.simulate_realtime && options_.sample_rate > 0) {
            const auto now = std::chrono::steady_clock::now();
            if (!started_) { started_ = true; start_ = now; cursor_ = 0; }
            const double elapsed = std::chrono::duration<double>(now - start_).count();
            const auto   due     = static_cast<std::size_t>(elapsed * options_.sample_rate);
            take = (due > cursor_) ? std::min(due - cursor_, samples_.size() - cursor_) : 0;
        }
        if (take == 0) { return {}; }

        std::vector<GazeSample> out{samples_.begin() + static_cast<std::ptrdiff_t>(cursor_),
                                    samples_.begin() + static_cast<std::ptrdiff_t>(cursor_ + take)};
        cursor_ += take;
        return out;
    }

    [[nodiscard]] bool is_connected() const override { return !samples_.empty(); }
    [[nodiscard]] bool is_streaming() const override { return cursor_ < samples_.size(); }

    /// Whether every loaded sample has been handed out.
    [[nodiscard]] bool exhausted() const noexcept { return cursor_ >= samples_.size(); }
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }

private:
    static std::vector<std::string> split(const std::string& line)
    {
        std::vector<std::string> out;
        std::string              cell;
        std::istringstream       in{line};
        while (std::getline(in, cell, ',')) {
            // A file written on Windows ends every line with \r, which would
            // otherwise become part of the last header name and stop that
            // column matching.
            if (!cell.empty() && cell.back() == '\r') { cell.pop_back(); }
            out.push_back(cell);
        }
        return out;
    }

    /// Parse a cell, independent of the process locale.
    ///
    /// std::stod honours LC_NUMERIC, so under a comma-decimal locale "1.5"
    /// silently becomes 1.0 and every coordinate in the file is mangled with
    /// no error anywhere.
    static bool to_double(const std::string& text, double& out)
    {
        const auto value = detail::parse_double(text);
        if (!value) { return false; }
        out = *value;
        return true;
    }

    void read_file(const std::filesystem::path& path)
    {
        std::ifstream in{path};
        if (!in) { throw std::runtime_error("DefaultTracker: cannot open " + path.string()); }

        std::string header;
        if (!std::getline(in, header)) { return; }   // empty file is not an error
        const std::vector<std::string> columns = split(header);

        std::unordered_map<std::string, std::size_t> index;
        for (std::size_t i = 0; i < columns.size(); ++i) { index[columns[i]] = i; }
        if (index.count("timestamp") == 0 && index.count("timestamp_ns") == 0) {
            throw std::runtime_error("DefaultTracker: " + path.string() +
                                     " has no 'timestamp' or 'timestamp_ns' column");
        }

        std::string row;
        while (std::getline(in, row)) {
            if (row.empty()) { continue; }
            const std::vector<std::string> cells = split(row);
            GazeSample                     s     = make_gaze_sample();

            const auto get = [&](const char* name, double& value) {
                const auto it = index.find(name);
                return it != index.end() && it->second < cells.size() &&
                       to_double(cells[it->second], value);
            };

            // `timestamp` is MILLISECONDS, written as a float
            // ("1787694423228.0"). That is the SDK's own output convention:
            // the Python SDK converts the same column with
            // `timestamp_ns = int(float(ts) * 1_000_000)` (_core.py). Reading
            // it as nanoseconds puts every recording a million times too close
            // together, and parsing it as an integer rejects the ".0" and
            // replays the file as empty.
            //
            // A `timestamp_ns` column, where a producer writes one, is already
            // in nanoseconds and wins.
            //
            // Scaled digit by digit rather than through a double, because
            // epoch nanoseconds exceed what a double represents exactly and a
            // silent shift of a few hundred nanoseconds is the kind of thing
            // that breaks a golden-fixture comparison for no reason.
            // Tried in order rather than chosen by which column exists: a file
            // carrying both, which is what a producer mid-migration writes,
            // would otherwise replay as nothing at all if timestamp_ns is
            // blank on every row.
            std::optional<std::int64_t> ts;
            if (const auto ns_cell = index.find("timestamp_ns");
                ns_cell != index.end() && ns_cell->second < cells.size()) {
                ts = detail::parse_scaled(cells[ns_cell->second], 0);
            }
            if (!ts) {
                if (const auto ms_cell = index.find("timestamp");
                    ms_cell != index.end() && ms_cell->second < cells.size()) {
                    ts = detail::parse_scaled(cells[ms_cell->second], 6);
                }
            }
            if (!ts) { continue; }
            s.timestamp_ns = *ts;

            double v = 0.0;

            if (get("leftEyeX", v))  { s.left_eye_x  = static_cast<float>(v); }
            if (get("leftEyeY", v))  { s.left_eye_y  = static_cast<float>(v); }
            if (get("rightEyeX", v)) { s.right_eye_x = static_cast<float>(v); }
            if (get("rightEyeY", v)) { s.right_eye_y = static_cast<float>(v); }
            if (get("pupil_diameter_left", v))  { s.pupil_diameter_left  = v; }
            if (get("pupil_diameter_right", v)) { s.pupil_diameter_right = v; }
            if (get("PupilDiameter", v))        { s.pupil_diameter       = v; }
            if (get("Blink_ID", v))             { s.blink_id = static_cast<std::int32_t>(v); }

            if (get("optical_axis_left_x", v)) { s.optical_axis_left_x = static_cast<float>(v); }
            if (get("optical_axis_left_y", v)) { s.optical_axis_left_y = static_cast<float>(v); }
            if (get("optical_axis_left_z", v)) { s.optical_axis_left_z = static_cast<float>(v); }
            if (get("optical_axis_right_x", v)) { s.optical_axis_right_x = static_cast<float>(v); }
            if (get("optical_axis_right_y", v)) { s.optical_axis_right_y = static_cast<float>(v); }
            if (get("optical_axis_right_z", v)) { s.optical_axis_right_z = static_cast<float>(v); }

            if (get("eyeball_center_left_x", v)) { s.eyeball_center_left_x = static_cast<float>(v); }
            if (get("eyeball_center_left_y", v)) { s.eyeball_center_left_y = static_cast<float>(v); }
            if (get("eyeball_center_left_z", v)) { s.eyeball_center_left_z = static_cast<float>(v); }
            if (get("eyelid_angle_top_left", v))     { s.eyelid_angle_top_left     = static_cast<float>(v); }
            if (get("eyelid_angle_bottom_left", v))  { s.eyelid_angle_bottom_left  = static_cast<float>(v); }
            if (get("eyelid_angle_top_right", v))    { s.eyelid_angle_top_right    = static_cast<float>(v); }
            if (get("eyelid_angle_bottom_right", v)) { s.eyelid_angle_bottom_right = static_cast<float>(v); }

            if (get("eyeball_center_right_x", v)) { s.eyeball_center_right_x = static_cast<float>(v); }
            if (get("eyeball_center_right_y", v)) { s.eyeball_center_right_y = static_cast<float>(v); }
            if (get("eyeball_center_right_z", v)) { s.eyeball_center_right_z = static_cast<float>(v); }

            samples_.push_back(s);
        }
    }

    Options                               options_;
    std::vector<GazeSample>               samples_;
    std::size_t                           cursor_ = 0;
    bool                                  started_ = false;
    std::chrono::steady_clock::time_point start_{};
};

/// Holds a tracker that is supplied after construction.
///
/// The counterpart to the Python SDK's WebcamPlatform, where the application
/// decides at runtime which source to attach. Without one it reports
/// disconnected and yields nothing, rather than failing.
///
/// Does not own the tracker: whatever is passed to set_tracker() must outlive
/// this object.
class WebcamPlatform : public EyeTracker {
public:
    WebcamPlatform() = default;
    explicit WebcamPlatform(EyeTracker* tracker) noexcept : tracker_(tracker) {}

    /// Swap the underlying source. Null detaches.
    void set_tracker(EyeTracker* tracker) noexcept { tracker_ = tracker; }
    [[nodiscard]] EyeTracker* tracker() const noexcept { return tracker_; }

    [[nodiscard]] std::vector<GazeSample> get_buffered_data() override
    {
        return tracker_ != nullptr ? tracker_->get_buffered_data() : std::vector<GazeSample>{};
    }
    [[nodiscard]] bool is_connected() const override
    {
        return tracker_ != nullptr && tracker_->is_connected();
    }
    [[nodiscard]] bool is_streaming() const override
    {
        return tracker_ != nullptr && tracker_->is_streaming();
    }
    void close() override { if (tracker_ != nullptr) { tracker_->close(); } }

private:
    EyeTracker* tracker_ = nullptr;
};

}  // namespace harmoneyes::theia
