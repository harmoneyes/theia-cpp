#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

// Locale-independent number parsing.
//
// Two reasons this exists rather than calling std::stod.
//
// std::stod honours LC_NUMERIC. Under a comma-decimal locale (de_DE, fr_FR and
// many others) "1.5" parses as 1.0, silently, with no error anywhere. The wire
// and file formats this SDK reads are always C-locale, so a user's locale must
// not change how they are interpreted.
//
// std::from_chars for floating point is the right tool but is not universally
// available: libc++ only implemented it in LLVM 20, and macOS is a supported
// platform. So it is used where present and an explicitly classic-locale
// stringstream stands in where it is not.

namespace harmoneyes::theia::detail {

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#  define HARMONEYES_THEIA_HAS_FP_FROM_CHARS 1
#else
#  define HARMONEYES_THEIA_HAS_FP_FROM_CHARS 0
#endif

/// Parse a complete C-locale decimal. Trailing characters make it fail.
[[nodiscard]] inline std::optional<double> parse_double(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    if (text.empty()) { return std::nullopt; }

#if HARMONEYES_THEIA_HAS_FP_FROM_CHARS
    double     value  = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) { return std::nullopt; }
    return value;
#else
    std::istringstream in{std::string{text}};
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail() || in.peek() != std::istringstream::traits_type::eof()) { return std::nullopt; }
    return value;
#endif
}

/// Parse a complete 64-bit integer, without going through double.
///
/// Timestamps are nanoseconds since the epoch, which exceeds what a double
/// represents exactly: 1763140928903088511 comes back as ...088384 through a
/// double, a 127 ns shift for no reason.
[[nodiscard]] inline std::optional<std::int64_t> parse_int64(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    if (text.empty()) { return std::nullopt; }

    std::int64_t value  = 0;
    const auto   result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) { return std::nullopt; }
    return value;
}

}  // namespace harmoneyes::theia::detail
