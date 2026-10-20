#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "harmoneyes/theia/alerts.hpp"
#include "harmoneyes/theia/detail/borrowed.hpp"
#include "harmoneyes/theia/detail/c_api.hpp"
#include "harmoneyes/theia/detail/check.hpp"
#include "harmoneyes/theia/types.hpp"

namespace harmoneyes::theia {

// ── Results ──────────────────────────────────────────────────────────────

/// A cognitive-load prediction.
struct CogLoadResult {
    CogLoadLevel level        = CogLoadLevel::Low;
    float        confidence   = 0.0F;
    float        prob_high    = 0.0F;  ///< raw P(High), before thresholding
    float        prob_low     = 0.0F;
    int          batch_number = 0;
    std::int64_t timestamp_ns = 0;
    std::string  model_label;
};

/// A fatigue prediction.
struct FatigueResult {
    SleepinessLevel    level        = SleepinessLevel::Alert;
    float              confidence   = 0.0F;
    int                batch_number = 0;
    std::optional<int> ttt_level;    ///< time-to-transition: the next level
    std::optional<int> ttt_minutes;  ///< minutes until that transition
};

/// An attentional-style classification.
struct AttentionResult {
    AttentionLevel level             = AttentionLevel::Neither;
    std::string    label;
    double         composite_score   = 0.0;  ///< 0..1
    int            total_observations = 0;
};

/// One loaded model's identity.
struct ModelInfo {
    std::string   name;
    std::uint32_t version = 0;
    std::string   commit;
};

/// A labelled instant, for baselining and refining.
using GroundTruthPoint = ::ground_truth_point_t;

/// How cognitive load was distributed over the session.
struct MentalReadinessResult {
    double low_percentage      = 0.0;
    double moderate_percentage = 0.0;
    double high_percentage     = 0.0;
    int    total_predictions   = 0;
};

// ── Options ──────────────────────────────────────────────────────────────

/// How to construct the SDK.
///
/// Every field has a usable default, so `Options{}` is a valid starting point
/// for a Pupil Labs session. Callbacks fire on the SDK's own threads, not the
/// caller's; anything they touch needs its own synchronisation.
struct Options {
    /// Where session files are written. Empty writes nothing.
    std::string output_dir = "sdk_files";

    /// Which tracker preset to run.
    Config config{};

    /// The device lease. Released builds refuse to construct without one.
    std::string lease_token;
    std::string lease_expected_device_id;  ///< verify the lease names this device
    std::string lease_expected_tenant_id;  ///< verify the lease names this tenant

    bool enable_cog_load_model = true;
    bool enable_fatigue_model  = true;

    /// Where decrypted models are cached. Empty uses $THEIA_MODEL_CACHE_DIR,
    /// then ~/.theia/models.
    std::string model_cache_dir;
    /// Where models are fetched from. Empty uses the URL baked into the build.
    std::string cdn_url;

    std::function<void(const std::string&)> on_log;
    std::function<void(const CogLoadResult&)> on_cog_load;
    std::function<void(const FatigueResult&)> on_fatigue;
};

// ── The SDK ──────────────────────────────────────────────────────────────

/// The eye-tracking pipeline.
///
/// Move-only and non-copyable: it owns a native handle and the callback state
/// the library holds pointers into.
///
/// Threading follows the library's own contract. add_gaze_samples() is single
/// writer: call it from one thread at a time, and never while
/// start_data_streaming() is feeding the pipeline itself.
class TheiaSDK {
    // Declared first so the nested Session can name it. Defined below, after
    // the class, because it holds the callback trampolines.
    struct State;

public:
    explicit TheiaSDK(Options options);
    ~TheiaSDK();

    TheiaSDK(TheiaSDK&& other) noexcept;
    TheiaSDK& operator=(TheiaSDK&& other) noexcept;
    TheiaSDK(const TheiaSDK&)            = delete;
    TheiaSDK& operator=(const TheiaSDK&) = delete;

    /// An open session. Stops it on destruction.
    ///
    /// Destruction is best-effort and never throws; call stop() to find out
    /// whether stopping actually worked.
    ///
    /// Stopping a session also stops data streaming, because that is what the
    /// native stop_session does. If you started the tracker with
    /// start_data_streaming(), letting a Session go out of scope tears the
    /// tracker thread down too.
    ///
    /// Safe to outlive its TheiaSDK: it holds a weak reference and becomes a
    /// no-op once the SDK is gone.
    class Session {
    public:
        ~Session() noexcept;
        Session(Session&& other) noexcept;
        Session& operator=(Session&& other) noexcept;
        Session(const Session&)            = delete;
        Session& operator=(const Session&) = delete;

        /// Stop now, reporting failure. A no-op if already stopped or if the
        /// owning TheiaSDK is gone.
        void stop();

    private:
        friend class TheiaSDK;
        explicit Session(std::weak_ptr<State> state) noexcept : state_(std::move(state)) {}

        /// Detach and return the handle, or null if there is nothing to stop.
        ::theia_sdk_t* take_handle() noexcept;
        void           stop_quietly() noexcept;

        std::weak_ptr<State> state_;
    };

    /// Begin a session. Dropping the returned handle stops it immediately.
    [[nodiscard]] Session start_session(const std::string& session_uuid,
                                        const std::string& task_type = "Computer Task");

    /// Feed samples in. Single writer; see the class note on threading.
    void add_gaze_samples(const GazeSample* first, std::size_t count);
    void add_gaze_samples(const std::vector<GazeSample>& samples)
    {
        add_gaze_samples(samples.data(), samples.size());
    }

    // Polling getters. Two of them consume, two do not, which the library
    // decides and this cannot change.
    //
    // cog_load() and fatigue() are event streams: each returns the latest
    // result and CLEARS it, so a second call before the next window returns
    // nothing.
    //
    // attention() and mental_readiness() are rolling state: they return the
    // current classification every time they are called and clear nothing.
    // Reading the same value twice means no new window has landed, not that a
    // new window agreed with the last one.
    [[nodiscard]] std::optional<CogLoadResult>         cog_load() const;
    [[nodiscard]] std::optional<FatigueResult>         fatigue() const;
    [[nodiscard]] std::optional<AttentionResult>       attention() const;
    [[nodiscard]] std::optional<MentalReadinessResult> mental_readiness(double elapsed_seconds = 0.0) const;

    /// Every cognitive-load prediction so far, oldest first.
    [[nodiscard]] std::vector<CogLoadResult> cog_load_history() const;

    // Tracker control, for devices the library connects to itself.

    /// Point the SDK at a Ganzin Sol. Ganzin has no discovery, so this must be
    /// called before start_data_streaming(). `port` is the HTTP control port,
    /// `rtsp_port` the stream. Recorded but unused for other devices.
    void set_ganzin_connection(const std::string& ip, int port = 8080, int rtsp_port = 8086);

    /// Pin which Tobii Pro Glasses 3 to use. Optional: G3 advertises itself
    /// over mDNS, and an empty `ip` takes the first device that answers. Set
    /// it to choose between several, or where multicast is blocked.
    void set_tobii_g3_connection(const std::string& ip = {}, int port = 80, int rtsp_port = 8554);

    /// Set the G3's device-side gaze rate, 50 or 100 Hz. Returns false if the
    /// library rejected it. Anything other than 50 or 100 is rejected rather
    /// than clamped, because a wrong rate quietly degrades every eye-movement
    /// feature. Must be called before start_session().
    ///
    /// **100 Hz leaves the legacy feature extractor behind.** That extractor
    /// captures the nominal sample rate when the SDK is constructed, and the C
    /// ABI's options_t carries no field for the G3 rate, so it is always built
    /// for the 50 Hz default. Raising the rate afterwards switches the device
    /// and the ACE pipeline but not the extractor, and the library only warns
    /// about the mismatch when the device *refuses* the rate -- so the case
    /// that goes wrong is the one that appears to succeed. Until the C ABI can
    /// carry the rate at construction, prefer 50 Hz, or disable legacy feature
    /// extraction and rely on the ACE outputs alone.
    [[nodiscard]] bool set_tobii_g3_gaze_frequency(int hz);

    /// Run the G3's own calibration. Returns whether the device accepted it.
    [[nodiscard]] bool calibrate_tobii_g3();

    void start_data_streaming();
    void stop_data_streaming();
    [[nodiscard]] bool is_tracker_connected() const;

    /// Whether samples are actually arriving.
    ///
    /// Distinct from is_tracker_connected(): a device can stay connected while
    /// producing nothing, which reads as healthy if you only ask the former.
    [[nodiscard]] bool is_tracker_streaming() const;

    [[nodiscard]] bool is_running() const;
    [[nodiscard]] int  batch_count() const;
    [[nodiscard]] int  batch_size() const;
    [[nodiscard]] bool has_connection_timeout() const;

    // Scene video, for trackers that provide it. Frames are pulled, not
    // pushed; video_frame() returns nothing until one is available.
    void start_video();
    void stop_video();
    [[nodiscard]] bool is_video_streaming() const;
    [[nodiscard]] std::optional<VideoFrame> video_frame() const;

    // Eye-camera video, same shape as the scene calls above.
    void start_eye_video();
    void stop_eye_video();
    [[nodiscard]] bool is_eye_video_streaming() const;
    [[nodiscard]] std::optional<VideoFrame> eye_video_frame() const;

    // Tuning. All of these take effect from the next window.

    /// Shift the clock the fatigue model reads time-of-day from.
    ///
    /// The model encodes time of day, so a recording replayed at a different
    /// hour than it was captured scores differently. Set this per session when
    /// replaying, as the Python SDK's own documentation also advises.
    void set_fatigue_time_offset_seconds(double seconds);

    /// How long a session must run before mental readiness reports anything.
    void set_mental_readiness_min_session_seconds(double seconds);
    void set_mental_readiness_low_high_threshold(double threshold);
    void set_mental_readiness_report_windows(const std::vector<double>& nominal_window_seconds);

    void set_window_size(float seconds);
    void set_graph_start_time(double unix_seconds);
    void set_sdk_model_confidence_threshold(float threshold);
    void set_cog_load_model_settings(int consensus_window_size, float consensus_threshold,
                                     float low_confidence_threshold,
                                     float high_confidence_threshold);

    // Session metadata, recorded alongside the results.
    void set_profile(const std::string& profile_name);
    void set_task(const std::string& task_name);
    void set_environment(const std::string& environment_name);
    void set_lambda_url(const std::string& url);
    [[nodiscard]] std::string profile() const;
    [[nodiscard]] std::string task() const;
    [[nodiscard]] std::string environment() const;

    /// Which models are loaded, and at what version.
    [[nodiscard]] std::vector<ModelInfo> model_info() const;

    // Personalising the cognitive-load model to one person.
    void process_baselining_data(double start_time_sec, double end_time_sec,
                                 const std::vector<GroundTruthPoint>& ground_truth);
    void process_general_model_baseline(double easy_start, double easy_end, double hard_start,
                                        double hard_end, int easy_gt, int hard_gt);
    void process_refining_data(const std::vector<GroundTruthPoint>& ground_truth,
                               double baselining_end_time, const std::string& refining_model);
    void set_refine_settings(float growth_rate, double lookback_seconds, float refine_eta);
    void save_refined_model(const std::string& path) const;
    void load_refined_model(const std::string& path);
    void reset_to_base_model();

    // Alert rules.
    void set_alert_rules(const std::string& rules_json);
    void set_alert_callback(AlertCallback callback);
    [[nodiscard]] std::vector<AlertRuleInfo> list_alert_rules() const;

    /// Where the rules came from and how sync stands.
    ///
    /// The C ABI returns this by value with no error channel, and reports a
    /// failure as a zeroed struct. A failure is therefore indistinguishable
    /// from "nothing configured": read rule_count == 0 as "no rules are being
    /// evaluated", not as proof that none were loaded.
    [[nodiscard]] AlertRulesStatus alert_rules_status() const;
    void set_alert_subject(const std::string& subject);
    void enable_alert_sync(const std::string& api_base_url, int interval_seconds = 60,
                           bool report_firings = true);
    void disable_alert_sync();
    void refresh_alert_rules();

    /// Replace the device lease, for example after renewing it.
    ///
    /// Throws a LeaseVerificationError subclass and keeps the previous lease
    /// if the new one does not verify.
    void set_lease_token(const std::string& lease_token);

    /// Release tracker, video and session resources early. The destructor does
    /// this anyway.
    void cleanup();

private:
    /// The native handle, or throw if this object was moved from.
    ///
    /// Every C entry point dereferences the handle without a null check, so
    /// passing a moved-from handle through would segfault inside the prebuilt
    /// library with a stack that points nowhere useful.
    [[nodiscard]] ::theia_sdk_t* handle() const;

    /// Shared rather than unique so a Session can hold a weak reference and
    /// go quiet when the SDK dies, instead of stopping a freed session.
    std::shared_ptr<State> state_;
};

// ── Implementation ───────────────────────────────────────────────────────

namespace detail {

[[nodiscard]] inline CogLoadResult to_cog_load(const ::cog_load_result_t& c)
{
    CogLoadResult r;
    r.level        = static_cast<CogLoadLevel>(c.level);
    r.confidence   = c.confidence;
    r.prob_high    = c.prob_high;
    r.prob_low     = c.prob_low;
    r.batch_number = c.batch_number;
    r.timestamp_ns = c.timestamp_ns;
    r.model_label  = copy_borrowed(c.model_label);
    return r;
}

[[nodiscard]] inline FatigueResult to_fatigue(const ::fatigue_result_t& c)
{
    FatigueResult r;
    r.level        = static_cast<SleepinessLevel>(c.level);
    r.confidence   = c.confidence;
    r.batch_number = c.batch_number;
    // The C ABI flattens std::optional<int> to -1 for "absent".
    if (c.ttt_level >= 0)   { r.ttt_level   = c.ttt_level; }
    if (c.ttt_minutes >= 0) { r.ttt_minutes = c.ttt_minutes; }
    return r;
}

[[nodiscard]] inline AttentionResult to_attention(const ::attention_result_t& c)
{
    AttentionResult r;
    r.level              = static_cast<AttentionLevel>(c.level);
    r.label              = copy_borrowed(c.label);
    r.composite_score    = c.composite_score;
    r.total_observations = c.total_observations;
    return r;
}

[[nodiscard]] inline MentalReadinessResult to_mental_readiness(const ::mental_readiness_result_t& c)
{
    MentalReadinessResult r;
    r.low_percentage      = c.low_percentage;
    r.moderate_percentage = c.moderate_percentage;
    r.high_percentage     = c.high_percentage;
    r.total_predictions   = c.total_predictions;
    return r;
}

}  // namespace detail

/// Owns everything the library holds a pointer into.
///
/// Allocated before theia_sdk_create() and never moved, so the `this` pointer
/// handed to the library as user_data stays valid for the SDK's whole life,
/// including across a move of the owning TheiaSDK.
struct TheiaSDK::State {
    Options        options;
    AlertCallback  alert_callback;
    ::theia_sdk_t* handle       = nullptr;  ///< nulled by ~TheiaSDK
    bool           session_open = false;

    explicit State(Options o) : options(std::move(o)) {}

    // Trampolines. These run on the library's threads and are reached through
    // extern "C" frames, so an escaping exception is undefined behaviour.
    // Everything is swallowed here deliberately; report failures through
    // on_log instead.
    static void on_log(const char* message, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_log) { return; }
        try { self->options.on_log(detail::copy_borrowed(message)); } catch (...) {}
    }

    static void on_cog_load(::cog_load_result_t result, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_cog_load) { return; }
        try { self->options.on_cog_load(detail::to_cog_load(result)); } catch (...) {}
    }

    static void on_fatigue(::fatigue_result_t result, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->options.on_fatigue) { return; }
        try { self->options.on_fatigue(detail::to_fatigue(result)); } catch (...) {}
    }

    static void on_alert(::alert_event_t event, void* user_data) noexcept
    {
        auto* self = static_cast<State*>(user_data);
        if (self == nullptr || !self->alert_callback) { return; }
        try { self->alert_callback(detail::to_alert_event(event)); } catch (...) {}
    }
};

inline ::theia_sdk_t* TheiaSDK::Session::take_handle() noexcept
{
    const std::shared_ptr<State> live = state_.lock();
    state_.reset();
    if (!live || live->handle == nullptr) { return nullptr; }
    live->session_open = false;
    return live->handle;
}

inline void TheiaSDK::Session::stop_quietly() noexcept
{
    ::theia_sdk_t* h = take_handle();
    if (h != nullptr) { ::theia_sdk_stop_session(h); }
}

inline TheiaSDK::Session::~Session() noexcept { stop_quietly(); }

inline TheiaSDK::Session::Session(Session&& other) noexcept
    : state_(std::move(other.state_))
{
    other.state_.reset();
}

inline TheiaSDK::Session& TheiaSDK::Session::operator=(Session&& other) noexcept
{
    if (this != &other) {
        stop_quietly();
        state_ = std::move(other.state_);
        other.state_.reset();
    }
    return *this;
}

inline void TheiaSDK::Session::stop()
{
    ::theia_sdk_t* h = take_handle();
    if (h == nullptr) { return; }
    detail::check(::theia_sdk_stop_session(h), "theia_sdk_stop_session");
}

inline TheiaSDK::TheiaSDK(Options options)
    : state_(std::make_shared<State>(std::move(options)))
{
    const Options& o = state_->options;

    ::options_t c{};
    c.output_dir = o.output_dir.c_str();
    c.platform   = platform_string(o.config.device);

    // Inverted on purpose in the C ABI, so a zeroed options_t keeps both
    // models on. Invert back here so the C++ side reads positively.
    c.disable_cog_load_model = o.enable_cog_load_model ? 0 : 1;
    c.disable_fatigue_model  = o.enable_fatigue_model ? 0 : 1;

    if (!o.lease_token.empty())             { c.lease_token = o.lease_token.c_str(); }
    if (!o.lease_expected_device_id.empty()) { c.lease_expected_device_id = o.lease_expected_device_id.c_str(); }
    if (!o.lease_expected_tenant_id.empty()) { c.lease_expected_tenant_id = o.lease_expected_tenant_id.c_str(); }
    if (!o.model_cache_dir.empty())          { c.model_cache_dir = o.model_cache_dir.c_str(); }
    if (!o.cdn_url.empty())                  { c.cdn_url = o.cdn_url.c_str(); }

    if (o.on_log)      { c.log_callback = &State::on_log;                 c.log_callback_user_data = state_.get(); }
    if (o.on_cog_load) { c.cog_load_ace_callback = &State::on_cog_load;   c.cog_load_ace_callback_user_data = state_.get(); }
    if (o.on_fatigue)  { c.fatigue_callback = &State::on_fatigue;         c.fatigue_callback_user_data = state_.get(); }

    // c.config is left zeroed deliberately: platform_string() never returns an
    // empty string, and the library uses the preset instead of config_t
    // whenever platform is non-empty. If that ever changes, every tracker
    // would silently run at sample_rate 0.

    // create() is one of only two entry points that record a real error code.
    state_->handle = detail::check_handle(::theia_sdk_create(c), "theia_sdk_create");
}

inline ::theia_sdk_t* TheiaSDK::handle() const
{
    if (!state_ || state_->handle == nullptr) {
        throw UnknownError{ErrorCode::Unknown, "TheiaSDK: used after move"};
    }
    return state_->handle;
}

inline TheiaSDK::~TheiaSDK()
{
    // Order matters. destroy() joins every thread that can invoke a callback,
    // and those callbacks dereference State, so State must outlive the call.
    // Nulling the handle afterwards is what makes a surviving Session a no-op
    // rather than a use-after-free.
    if (state_ && state_->handle != nullptr) {
        ::theia_sdk_destroy(state_->handle);
        state_->handle = nullptr;
    }
}

inline TheiaSDK::TheiaSDK(TheiaSDK&& other) noexcept = default;

inline TheiaSDK& TheiaSDK::operator=(TheiaSDK&& other) noexcept
{
    if (this != &other) {
        // Same ordering requirement as the destructor.
        if (state_ && state_->handle != nullptr) {
            ::theia_sdk_destroy(state_->handle);
            state_->handle = nullptr;
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

inline TheiaSDK::Session TheiaSDK::start_session(const std::string& session_uuid,
                                                 const std::string& task_type)
{
    ::theia_sdk_t* h = handle();
    if (state_->session_open) {
        throw UnknownError{ErrorCode::Unknown,
                           "TheiaSDK::start_session: a session is already open"};
    }
    detail::check(::theia_sdk_start_session(h, session_uuid.c_str(), task_type.c_str()),
                  "theia_sdk_start_session");
    state_->session_open = true;
    return Session{std::weak_ptr<State>{state_}};
}

inline void TheiaSDK::add_gaze_samples(const GazeSample* first, std::size_t count)
{
    if (count == 0) { return; }
    // The C ABI takes a non-const pointer but does not write through it: it
    // copies each field into its own vector and applies sentinel rewrites
    // there.
    detail::check(::theia_sdk_add_gaze_samples(handle(), const_cast<GazeSample*>(first), count),
                  "theia_sdk_add_gaze_samples");
}

inline std::optional<CogLoadResult> TheiaSDK::cog_load() const
{
    ::cog_load_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_cog_load_ace(handle(), &c), "theia_sdk_get_cog_load_ace")) {
        return std::nullopt;
    }
    return detail::to_cog_load(c);
}

inline std::optional<FatigueResult> TheiaSDK::fatigue() const
{
    ::fatigue_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_fatigue(handle(), &c), "theia_sdk_get_fatigue")) {
        return std::nullopt;
    }
    return detail::to_fatigue(c);
}

// The ACE entry points, not the legacy ones, because that is what the Python
// SDK calls. The library keeps two of each: theia_sdk_get_attention reads a
// classification produced by the legacy 82-feature extractor on the legacy
// batch cadence, while theia_sdk_get_attention_ace reads the one produced per
// ACE window. Mental readiness likewise has two separate accumulators. Calling
// the legacy pair here would return different numbers from the identically
// named Python calls, with nothing to indicate it.
inline std::optional<AttentionResult> TheiaSDK::attention() const
{
    ::attention_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_attention_ace(handle(), &c),
                                "theia_sdk_get_attention_ace")) {
        return std::nullopt;
    }
    return detail::to_attention(c);
}

inline std::optional<MentalReadinessResult> TheiaSDK::mental_readiness(double elapsed_seconds) const
{
    ::mental_readiness_result_t c{};
    if (!detail::check_optional(
            ::theia_sdk_get_mental_readiness_ace(handle(), elapsed_seconds, &c),
            "theia_sdk_get_mental_readiness_ace")) {
        return std::nullopt;
    }
    return detail::to_mental_readiness(c);
}

inline std::vector<CogLoadResult> TheiaSDK::cog_load_history() const
{
    const ::cog_load_result_t* items = nullptr;
    std::size_t                count = 0;
    detail::check(::theia_sdk_get_cog_load_prediction_history(handle(), &items, &count),
                  "theia_sdk_get_cog_load_prediction_history");

    // Copied immediately: the array and every string in it live in a
    // thread-local buffer the next call on this thread overwrites.
    std::vector<CogLoadResult> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) { out.push_back(detail::to_cog_load(items[i])); }
    return out;
}

inline void TheiaSDK::set_ganzin_connection(const std::string& ip, int port, int rtsp_port)
{
    detail::check(::theia_sdk_set_ganzin_connection(handle(), ip.c_str(), port, rtsp_port),
                  "theia_sdk_set_ganzin_connection");
}

inline void TheiaSDK::set_tobii_g3_connection(const std::string& ip, int port, int rtsp_port)
{
    detail::check(::theia_sdk_set_tobii_g3_connection(handle(), ip.c_str(), port, rtsp_port),
                  "theia_sdk_set_tobii_g3_connection");
}

inline bool TheiaSDK::set_tobii_g3_gaze_frequency(int hz)
{
    return detail::check_bool(::theia_sdk_set_tobii_g3_gaze_frequency(handle(), hz),
                              "theia_sdk_set_tobii_g3_gaze_frequency");
}

inline bool TheiaSDK::calibrate_tobii_g3()
{
    return detail::check_bool(::theia_sdk_calibrate_tobii_g3(handle()),
                              "theia_sdk_calibrate_tobii_g3");
}

inline void TheiaSDK::start_data_streaming()
{
    detail::check(::theia_sdk_start_data_streaming(handle()), "theia_sdk_start_data_streaming");
}

inline void TheiaSDK::stop_data_streaming()
{
    detail::check(::theia_sdk_stop_data_streaming(handle()), "theia_sdk_stop_data_streaming");
}

inline bool TheiaSDK::is_tracker_connected() const
{
    return detail::check_bool(::theia_sdk_is_tracker_connected(handle()), "theia_sdk_is_tracker_connected");
}

inline bool TheiaSDK::is_running() const
{
    return detail::check_bool(::theia_sdk_is_running(handle()), "theia_sdk_is_running");
}

inline int TheiaSDK::batch_count() const
{
    return detail::check_count(::theia_sdk_batch_count(handle()), "theia_sdk_batch_count");
}

inline bool TheiaSDK::is_tracker_streaming() const
{
    return detail::check_bool(::theia_sdk_is_tracker_streaming(handle()),
                              "theia_sdk_is_tracker_streaming");
}

inline int TheiaSDK::batch_size() const
{
    return detail::check_count(::theia_sdk_batch_size(handle()), "theia_sdk_batch_size");
}

inline bool TheiaSDK::has_connection_timeout() const
{
    return detail::check_bool(::theia_sdk_has_connection_timeout(handle()),
                              "theia_sdk_has_connection_timeout");
}

namespace detail {

/// Copy a video_frame_t out of the library's thread-local buffers.
[[nodiscard]] inline VideoFrame to_video_frame(const ::video_frame_t& c)
{
    const auto copy = [](const std::uint8_t* data, std::size_t len) {
        return (data != nullptr && len != 0) ? std::vector<std::uint8_t>{data, data + len}
                                             : std::vector<std::uint8_t>{};
    };

    VideoFrame f;
    f.bgr_data               = copy(c.bgr_data, c.bgr_data_len);
    f.encoded_data           = copy(c.encoded_data, c.encoded_data_len);
    f.extradata              = copy(c.extradata, c.extradata_len);
    f.width                  = c.width;
    f.height                 = c.height;
    f.timestamp_unix_seconds = c.timestamp_unix_seconds;
    f.is_keyframe            = c.is_keyframe == 1;
    return f;
}

}  // namespace detail

inline void TheiaSDK::start_video()
{
    detail::check(::theia_sdk_start_video(handle()), "theia_sdk_start_video");
}

inline void TheiaSDK::stop_video()
{
    detail::check(::theia_sdk_stop_video(handle()), "theia_sdk_stop_video");
}

inline bool TheiaSDK::is_video_streaming() const
{
    return detail::check_bool(::theia_sdk_is_video_streaming(handle()),
                              "theia_sdk_is_video_streaming");
}

inline std::optional<VideoFrame> TheiaSDK::video_frame() const
{
    ::video_frame_t c{};
    if (!detail::check_optional(::theia_sdk_get_video_frame(handle(), &c),
                                "theia_sdk_get_video_frame")) {
        return std::nullopt;
    }
    return detail::to_video_frame(c);
}

inline void TheiaSDK::start_eye_video()
{
    detail::check(::theia_sdk_start_eye_video(handle()), "theia_sdk_start_eye_video");
}

inline void TheiaSDK::stop_eye_video()
{
    detail::check(::theia_sdk_stop_eye_video(handle()), "theia_sdk_stop_eye_video");
}

inline bool TheiaSDK::is_eye_video_streaming() const
{
    return detail::check_bool(::theia_sdk_is_eye_video_streaming(handle()),
                              "theia_sdk_is_eye_video_streaming");
}

inline std::optional<VideoFrame> TheiaSDK::eye_video_frame() const
{
    ::video_frame_t c{};
    if (!detail::check_optional(::theia_sdk_get_eye_video_frame(handle(), &c),
                                "theia_sdk_get_eye_video_frame")) {
        return std::nullopt;
    }
    return detail::to_video_frame(c);
}

inline void TheiaSDK::set_fatigue_time_offset_seconds(double seconds)
{
    detail::check(::theia_sdk_set_fatigue_time_offset_seconds(handle(), seconds),
                  "theia_sdk_set_fatigue_time_offset_seconds");
}

inline void TheiaSDK::set_mental_readiness_min_session_seconds(double seconds)
{
    detail::check(::theia_sdk_set_mental_readiness_min_session_seconds(handle(), seconds),
                  "theia_sdk_set_mental_readiness_min_session_seconds");
}

inline void TheiaSDK::set_mental_readiness_low_high_threshold(double threshold)
{
    detail::check(::theia_sdk_set_mental_readiness_low_high_threshold(handle(), threshold),
                  "theia_sdk_set_mental_readiness_low_high_threshold");
}

inline void TheiaSDK::set_mental_readiness_report_windows(
    const std::vector<double>& nominal_window_seconds)
{
    // The C ABI takes a non-const pointer but copies out of it, the same as
    // add_gaze_samples. An empty list must not dereference .data().
    detail::check(::theia_sdk_set_mental_readiness_report_windows(
                      handle(), const_cast<double*>(nominal_window_seconds.data()),
                      nominal_window_seconds.size()),
                  "theia_sdk_set_mental_readiness_report_windows");
}

inline void TheiaSDK::set_window_size(float seconds)
{
    detail::check(::theia_sdk_set_window_size(handle(), seconds), "theia_sdk_set_window_size");
}

inline void TheiaSDK::set_graph_start_time(double unix_seconds)
{
    detail::check(::theia_sdk_set_graph_start_time(handle(), unix_seconds),
                  "theia_sdk_set_graph_start_time");
}

inline void TheiaSDK::set_sdk_model_confidence_threshold(float threshold)
{
    detail::check(::theia_sdk_set_sdk_model_confidence_threshold(handle(), threshold),
                  "theia_sdk_set_sdk_model_confidence_threshold");
}

inline void TheiaSDK::set_cog_load_model_settings(int consensus_window_size,
                                                  float consensus_threshold,
                                                  float low_confidence_threshold,
                                                  float high_confidence_threshold)
{
    detail::check(::theia_sdk_set_cog_load_model_settings(handle(), consensus_window_size,
                                                          consensus_threshold,
                                                          low_confidence_threshold,
                                                          high_confidence_threshold),
                  "theia_sdk_set_cog_load_model_settings");
}

inline void TheiaSDK::set_profile(const std::string& profile_name)
{
    detail::check(::theia_sdk_set_profile(handle(), profile_name.c_str()), "theia_sdk_set_profile");
}

inline void TheiaSDK::set_task(const std::string& task_name)
{
    detail::check(::theia_sdk_set_task(handle(), task_name.c_str()), "theia_sdk_set_task");
}

inline void TheiaSDK::set_environment(const std::string& environment_name)
{
    detail::check(::theia_sdk_set_environment(handle(), environment_name.c_str()),
                  "theia_sdk_set_environment");
}

inline void TheiaSDK::set_lambda_url(const std::string& url)
{
    detail::check(::theia_sdk_set_lambda_url(handle(), url.c_str()), "theia_sdk_set_lambda_url");
}

inline std::string TheiaSDK::profile() const
{
    return detail::copy_borrowed(::theia_sdk_get_profile(handle()));
}

inline std::string TheiaSDK::task() const
{
    return detail::copy_borrowed(::theia_sdk_get_task(handle()));
}

inline std::string TheiaSDK::environment() const
{
    return detail::copy_borrowed(::theia_sdk_get_environment(handle()));
}

inline std::vector<ModelInfo> TheiaSDK::model_info() const
{
    const ::model_info_t* items = nullptr;
    std::size_t           count = 0;
    detail::check(::theia_sdk_get_model_info(handle(), &items, &count),
                  "theia_sdk_get_model_info");

    // Copied immediately: thread-local buffer, overwritten by the next call.
    std::vector<ModelInfo> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(ModelInfo{detail::copy_borrowed(items[i].name), items[i].version,
                                detail::copy_borrowed(items[i].commit)});
    }
    return out;
}

inline void TheiaSDK::process_baselining_data(double start_time_sec, double end_time_sec,
                                              const std::vector<GroundTruthPoint>& ground_truth)
{
    detail::check(::theia_sdk_process_baselining_data(
                      handle(), start_time_sec, end_time_sec,
                      const_cast<GroundTruthPoint*>(ground_truth.data()), ground_truth.size()),
                  "theia_sdk_process_baselining_data");
}

inline void TheiaSDK::process_general_model_baseline(double easy_start, double easy_end,
                                                     double hard_start, double hard_end,
                                                     int easy_gt, int hard_gt)
{
    detail::check(::theia_sdk_process_general_model_baseline(handle(), easy_start, easy_end,
                                                             hard_start, hard_end, easy_gt,
                                                             hard_gt),
                  "theia_sdk_process_general_model_baseline");
}

inline void TheiaSDK::process_refining_data(const std::vector<GroundTruthPoint>& ground_truth,
                                            double             baselining_end_time,
                                            const std::string& refining_model)
{
    detail::check(::theia_sdk_process_refining_data(
                      handle(), const_cast<GroundTruthPoint*>(ground_truth.data()),
                      ground_truth.size(), baselining_end_time, refining_model.c_str()),
                  "theia_sdk_process_refining_data");
}

inline void TheiaSDK::set_refine_settings(float growth_rate, double lookback_seconds,
                                          float refine_eta)
{
    detail::check(::theia_sdk_set_refine_settings(handle(), growth_rate, lookback_seconds,
                                                  refine_eta),
                  "theia_sdk_set_refine_settings");
}

inline void TheiaSDK::save_refined_model(const std::string& path) const
{
    detail::check(::theia_sdk_save_refined_model(handle(), path.c_str()),
                  "theia_sdk_save_refined_model");
}

inline void TheiaSDK::load_refined_model(const std::string& path)
{
    detail::check(::theia_sdk_load_refined_model(handle(), path.c_str()),
                  "theia_sdk_load_refined_model");
}

inline void TheiaSDK::reset_to_base_model()
{
    detail::check(::theia_sdk_reset_to_base_model(handle()), "theia_sdk_reset_to_base_model");
}

inline void TheiaSDK::set_alert_rules(const std::string& rules_json)
{
    detail::check(::theia_sdk_set_alert_rules(handle(), rules_json.c_str()), "theia_sdk_set_alert_rules");
}

inline void TheiaSDK::set_alert_callback(AlertCallback callback)
{
    ::theia_sdk_t* h = handle();

    // Unregister first. The native set_alert_callback waits out any delivery
    // already under way, so after this returns nothing can be reading
    // state_->alert_callback while we replace it. Assigning before that drain
    // would race the library's data thread mid-callback.
    //
    // It deliberately skips that wait when called FROM the alert callback, and
    // that case is safe too: the assignment then happens on the delivery
    // thread itself.
    detail::check(::theia_sdk_set_alert_callback(h, nullptr, nullptr),
                  "theia_sdk_set_alert_callback");

    state_->alert_callback = std::move(callback);
    if (state_->alert_callback) {
        detail::check(::theia_sdk_set_alert_callback(h, &State::on_alert, state_.get()),
                      "theia_sdk_set_alert_callback");
    }
}

inline std::vector<AlertRuleInfo> TheiaSDK::list_alert_rules() const
{
    const ::alert_rule_info_t* rules = nullptr;
    std::size_t                count = 0;
    detail::check(::theia_sdk_list_alert_rules(handle(), &rules, &count), "theia_sdk_list_alert_rules");

    std::vector<AlertRuleInfo> out;
    out.reserve(count);
    // `rules` points at library-owned storage reused by the next call, so
    // every element is copied before returning.
    for (std::size_t i = 0; i < count; ++i) { out.push_back(detail::to_alert_rule_info(rules[i])); }
    return out;
}

inline AlertRulesStatus TheiaSDK::alert_rules_status() const
{
    // Returns by value and has no error channel.
    return detail::to_alert_rules_status(::theia_sdk_get_alert_rules_status(handle()));
}

inline void TheiaSDK::set_alert_subject(const std::string& subject)
{
    detail::check(::theia_sdk_set_alert_subject(handle(), subject.c_str()), "theia_sdk_set_alert_subject");
}

inline void TheiaSDK::enable_alert_sync(const std::string& api_base_url, int interval_seconds,
                                        bool report_firings)
{
    detail::check(::theia_sdk_enable_alert_sync(handle(), api_base_url.c_str(), interval_seconds,
                                                report_firings ? 1 : 0),
                  "theia_sdk_enable_alert_sync");
}

inline void TheiaSDK::disable_alert_sync()
{
    detail::check(::theia_sdk_disable_alert_sync(handle()), "theia_sdk_disable_alert_sync");
}

inline void TheiaSDK::refresh_alert_rules()
{
    detail::check(::theia_sdk_refresh_alert_rules(handle()), "theia_sdk_refresh_alert_rules");
}

inline void TheiaSDK::set_lease_token(const std::string& lease_token)
{
    // The other entry point that records a real error code, so the recorded
    // path is valid here and reports which lease check failed.
    if (::theia_sdk_set_lease_token(handle(), lease_token.c_str()) != 0) {
        detail::throw_recorded_error("theia_sdk_set_lease_token");
    }
}

inline void TheiaSDK::cleanup()
{
    detail::check(::theia_sdk_cleanup(handle()), "theia_sdk_cleanup");
}

}  // namespace harmoneyes::theia
