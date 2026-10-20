#pragma once

#include <string>

#include "harmoneyes/theia/detail/c_api.hpp"
#include "harmoneyes/theia/errors.hpp"

// Translating C ABI return codes into exceptions.
//
// Two things make this less obvious than it looks.
//
// First, the library records an error code and message in thread-local state,
// but ONLY theia_sdk_create() and theia_sdk_set_lease_token() ever write it,
// and they clear it only when they succeed. Every one of the other entry
// points returns -1 with that state untouched. So reading the channel after an
// unrelated failure reports whatever happened last, which is why there are two
// throw functions below rather than one.
//
// Second, nothing here may be called from an extern "C" callback trampoline.
// An exception unwinding into the prebuilt library's frames is undefined
// behaviour. Trampolines catch everything locally instead.

namespace harmoneyes::theia::detail {

/// Throw using the library's recorded error code and message.
///
/// Valid ONLY immediately after a failing theia_sdk_create() or
/// theia_sdk_set_lease_token(), on the thread that made the call. Those are
/// the only two entry points that populate the channel.
[[noreturn]] inline void throw_recorded_error(const char* context)
{
    const auto  code   = static_cast<ErrorCode>(::theia_get_last_error_code());
    const char* detail = ::theia_get_last_error_message();

    std::string message{context ? context : "theia"};
    message += ": ";
    message += (detail && *detail) ? detail : "no detail reported";

    switch (code) {
        case ErrorCode::ModelUnavailable:    throw ModelUnavailableError{code, message};
        case ErrorCode::LeaseMalformed:      throw LeaseMalformedError{code, message};
        case ErrorCode::LeaseUnknownKey:     throw LeaseUnknownKeyError{code, message};
        case ErrorCode::LeaseBadSignature:   throw LeaseSignatureError{code, message};
        case ErrorCode::LeaseExpired:        throw LeaseExpiredError{code, message};
        case ErrorCode::LeaseDeviceMismatch: throw LeaseDeviceMismatchError{code, message};
        case ErrorCode::LeaseTenantMismatch: throw LeaseTenantMismatchError{code, message};
        case ErrorCode::None:
        case ErrorCode::Unknown:
            break;
    }
    throw UnknownError{code, message};
}

/// Throw for an entry point that records nothing.
///
/// Deliberately does not consult the error channel: it would still hold the
/// result of some earlier create() or set_lease_token(), and reporting that
/// would name the wrong failure and throw the wrong exception type.
[[noreturn]] inline void throw_opaque_error(const char* context)
{
    throw UnknownError{ErrorCode::Unknown,
                       std::string{context ? context : "theia"} +
                           ": the library reported a failure but recorded no detail"};
}

// The C ABI uses several return shapes. Each gets its own helper so a call
// site cannot pick the wrong comparison.

/// Handle-returning constructors: null on failure.
/// The one shape where the recorded error state is meaningful.
[[nodiscard]] inline ::theia_sdk_t* check_handle(::theia_sdk_t* handle, const char* context)
{
    if (handle == nullptr) {
        throw_recorded_error(context);
    }
    return handle;
}

/// Mutators: 0 on success, -1 when the library caught an exception.
inline void check(int rc, const char* context)
{
    if (rc != 0) {
        throw_opaque_error(context);
    }
}

/// Optional getters: 0 wrote a value, 1 means nothing is available yet, -1
/// failed. Returns true when the out-parameter was written.
[[nodiscard]] inline bool check_optional(int rc, const char* context)
{
    if (rc == 0) {
        return true;
    }
    if (rc == 1) {
        return false;
    }
    throw_opaque_error(context);
}

/// Predicates: 1 true, 0 false, -1 failed.
///
/// Must compare against 1 explicitly. Writing `if (theia_sdk_is_running(h))`
/// reports true on failure, because -1 is truthy in C and C++.
[[nodiscard]] inline bool check_bool(int rc, const char* context)
{
    if (rc == 1) {
        return true;
    }
    if (rc == 0) {
        return false;
    }
    throw_opaque_error(context);
}

/// Counters: a non-negative value, or -1 on failure.
///
/// Only for counters whose underlying C++ accessor is noexcept and cannot
/// return a negative value, which is why -1 is unambiguous. A counter that may
/// legitimately be negative needs a different shape.
[[nodiscard]] inline int check_count(int rc, const char* context)
{
    if (rc < 0) {
        throw_opaque_error(context);
    }
    return rc;
}

}  // namespace harmoneyes::theia::detail
