#pragma once

#include <string>
#include <string_view>

#include "harmoneyes/theia/detail/c_api.hpp"

namespace harmoneyes::theia {

/// Version of this SDK package.
///
/// Distinct from version(): this is the header layer you compiled against,
/// that is the native library you are running. They move independently within
/// a release series, so a mismatch is worth reporting in a bug.
inline constexpr std::string_view kVersion = "0.1.0";

/// Version of the native Theia library.
///
/// The C ABI hands back a pointer into a thread-local buffer that the next call
/// to this same function overwrites, so the string is copied rather than referenced.
///
/// Deliberately does not throw, unlike the rest of this SDK. The underlying
/// call returns a compile-time constant and records nothing in the error
/// channel when it fails, so there would be nothing to report. Everything that
/// can genuinely fail throws; see errors.hpp.
[[nodiscard]] inline std::string version()
{
    const char* v = ::theia_sdk_version();
    return v ? std::string{v} : std::string{};
}

/// Version of the feature-extraction pipeline.
///
/// Tracks the feature set rather than the library, so it changes when feature
/// definitions change. Same borrowing and non-throwing rules as version().
[[nodiscard]] inline std::string feature_version()
{
    const char* v = ::theia_sdk_feature_version();
    return v ? std::string{v} : std::string{};
}

}  // namespace harmoneyes::theia
