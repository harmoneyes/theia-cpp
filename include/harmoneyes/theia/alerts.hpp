#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "harmoneyes/theia/detail/borrowed.hpp"
#include "harmoneyes/theia/detail/c_api.hpp"

namespace harmoneyes::theia {

/// One alert that fired.
///
/// Every string is owned. The C ABI delivers borrowed pointers valid only for
/// the duration of the callback, so they are copied before you see them.
struct AlertEvent {
    std::string  firing_id;
    std::string  rule_id;
    std::string  rule_name;
    std::string  input;              ///< which signal the rule watches
    int          level              = 0;
    int          previous_level     = 0;
    std::string  trigger;            ///< what caused the firing
    int          sustained_sec      = 0;
    int          level_share_percent = 0;
    std::string  message;
    std::int64_t timestamp_ns       = 0;
    std::int64_t fired_at_unix_ms   = 0;
};

/// A rule currently being evaluated.
struct AlertRuleInfo {
    std::string  id;
    std::string  name;
    std::string  summary;
    std::string  input;
    std::string  input_name;
    std::string  levels;
    bool         on_increase            = false;
    bool         on_decrease            = false;
    int          min_duration_sec       = 0;
    int          level_share_percent    = 0;
    int          level_share_window_sec = 0;
    int          cooldown_sec           = 0;
    int          readiness_window_sec   = 0;
    std::string  source;
    std::int64_t last_fired_at_unix_ms  = 0;
};

/// Where the rules came from and how synchronisation stands.
struct AlertRulesStatus {
    std::string  source;
    int          rule_count                 = 0;
    int          skipped_rule_count         = 0;
    std::string  skipped_detail;             ///< why a rule was left out
    std::string  etag;
    std::string  generated_at;
    std::string  tenant_id;
    std::string  subject;
    std::int64_t loaded_at_unix_ms          = 0;
    std::string  last_error;
    std::string  sync_state;
    std::string  sync_url;
    std::int64_t last_sync_attempt_unix_ms  = 0;
    std::int64_t last_sync_success_unix_ms  = 0;
    std::string  sync_error;
    int          pending_firings            = 0;
    std::int64_t last_firing_upload_unix_ms = 0;
    std::string  firing_upload_error;
};

/// Called once per fired alert, on the SDK's data thread rather than yours.
using AlertCallback = std::function<void(const AlertEvent&)>;

namespace detail {

[[nodiscard]] inline AlertEvent to_alert_event(const ::alert_event_t& c)
{
    AlertEvent e;
    e.firing_id           = copy_borrowed(c.firing_id);
    e.rule_id             = copy_borrowed(c.rule_id);
    e.rule_name           = copy_borrowed(c.rule_name);
    e.input               = copy_borrowed(c.input);
    e.level               = c.level;
    e.previous_level      = c.previous_level;
    e.trigger             = copy_borrowed(c.trigger);
    e.sustained_sec       = c.sustained_sec;
    e.level_share_percent = c.level_share_percent;
    e.message             = copy_borrowed(c.message);
    e.timestamp_ns        = c.timestamp_ns;
    e.fired_at_unix_ms    = c.fired_at_unix_ms;
    return e;
}

[[nodiscard]] inline AlertRuleInfo to_alert_rule_info(const ::alert_rule_info_t& c)
{
    AlertRuleInfo r;
    r.id                     = copy_borrowed(c.id);
    r.name                   = copy_borrowed(c.name);
    r.summary                = copy_borrowed(c.summary);
    r.input                  = copy_borrowed(c.input);
    r.input_name             = copy_borrowed(c.input_name);
    r.levels                 = copy_borrowed(c.levels);
    r.on_increase            = c.on_increase != 0;
    r.on_decrease            = c.on_decrease != 0;
    r.min_duration_sec       = c.min_duration_sec;
    r.level_share_percent    = c.level_share_percent;
    r.level_share_window_sec = c.level_share_window_sec;
    r.cooldown_sec           = c.cooldown_sec;
    r.readiness_window_sec   = c.readiness_window_sec;
    r.source                 = copy_borrowed(c.source);
    r.last_fired_at_unix_ms  = c.last_fired_at_unix_ms;
    return r;
}

[[nodiscard]] inline AlertRulesStatus to_alert_rules_status(const ::alert_rules_status_t& c)
{
    AlertRulesStatus s;
    s.source                     = copy_borrowed(c.source);
    s.rule_count                 = c.rule_count;
    s.skipped_rule_count         = c.skipped_rule_count;
    s.skipped_detail             = copy_borrowed(c.skipped_detail);
    s.etag                       = copy_borrowed(c.etag);
    s.generated_at               = copy_borrowed(c.generated_at);
    s.tenant_id                  = copy_borrowed(c.tenant_id);
    s.subject                    = copy_borrowed(c.subject);
    s.loaded_at_unix_ms          = c.loaded_at_unix_ms;
    s.last_error                 = copy_borrowed(c.last_error);
    s.sync_state                 = copy_borrowed(c.sync_state);
    s.sync_url                   = copy_borrowed(c.sync_url);
    s.last_sync_attempt_unix_ms  = c.last_sync_attempt_unix_ms;
    s.last_sync_success_unix_ms  = c.last_sync_success_unix_ms;
    s.sync_error                 = copy_borrowed(c.sync_error);
    s.pending_firings            = c.pending_firings;
    s.last_firing_upload_unix_ms = c.last_firing_upload_unix_ms;
    s.firing_upload_error        = copy_borrowed(c.firing_upload_error);
    return s;
}

}  // namespace detail
}  // namespace harmoneyes::theia
