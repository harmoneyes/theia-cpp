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

    // Polling getters. Each returns the latest result and clears it, so a
    // second call before the next window returns nothing.
    [[nodiscard]] std::optional<CogLoadResult>         cog_load() const;
    [[nodiscard]] std::optional<FatigueResult>         fatigue() const;
    [[nodiscard]] std::optional<AttentionResult>       attention() const;
    [[nodiscard]] std::optional<MentalReadinessResult> mental_readiness(double elapsed_seconds = 0.0) const;

    // Tracker control, for devices the library connects to itself.
    void start_data_streaming();
    void stop_data_streaming();
    [[nodiscard]] bool is_tracker_connected() const;
    [[nodiscard]] bool is_running() const;
    [[nodiscard]] int  batch_count() const;

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
    // there. Verified in theia_export.cpp's add_gaze_samples body.
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

inline std::optional<AttentionResult> TheiaSDK::attention() const
{
    ::attention_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_attention(handle(), &c), "theia_sdk_get_attention")) {
        return std::nullopt;
    }
    return detail::to_attention(c);
}

inline std::optional<MentalReadinessResult> TheiaSDK::mental_readiness(double elapsed_seconds) const
{
    ::mental_readiness_result_t c{};
    if (!detail::check_optional(::theia_sdk_get_mental_readiness(handle(), elapsed_seconds, &c),
                                "theia_sdk_get_mental_readiness")) {
        return std::nullopt;
    }
    return detail::to_mental_readiness(c);
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
