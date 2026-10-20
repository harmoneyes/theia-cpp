#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
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

/// Parse a decimal number and scale it by 10^`decimals`, exactly.
///
/// For a column written in one unit and needed in a smaller one: a timestamp
/// recorded as milliseconds ("1787694423228.0") that the pipeline wants in
/// nanoseconds. Two things rule out the obvious routes. parse_int64 rejects
/// the trailing ".0" and drops the row; a double cannot hold epoch
/// nanoseconds exactly, so the value arrives shifted. This scales the digits
/// instead.
///
/// Fractional digits past `decimals` are truncated, which is what the scaling
/// discards anyway. Anything not in plain decimal form -- scientific notation,
/// say -- falls back to a double rather than failing the row outright.
[[nodiscard]] inline std::optional<std::int64_t> parse_scaled(std::string_view text, int decimals)
{
    const std::string_view original = text;

    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    if (text.empty() || decimals < 0) { return std::nullopt; }

    bool negative = false;
    if (text.front() == '+' || text.front() == '-') {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }

    const auto dot       = text.find('.');
    const auto int_part  = text.substr(0, dot);
    const auto frac_part = (dot == std::string_view::npos) ? std::string_view{} : text.substr(dot + 1);

    const auto all_digits = [](std::string_view s) {
        return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (int_part.empty() && frac_part.empty()) { return std::nullopt; }
    if (!all_digits(int_part) || !all_digits(frac_part)) {
        const auto value = parse_double(original);
        if (!value) { return std::nullopt; }

        // parse_double accepts "nan" and "inf", and the scaling can overflow.
        // Converting either to an integer is undefined behaviour, and on x86
        // it yields INT64_MIN -- a value that passes an `if (!result)` check
        // and travels on as a real timestamp. Reject instead.
        const double scaled = *value * std::pow(10.0, decimals);
        if (!std::isfinite(scaled) ||
            scaled >= 9223372036854775808.0 ||   // 2^63, the first unrepresentable value
            scaled < -9223372036854775808.0) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(scaled);
    }

    std::int64_t value = 0;
    const auto   push  = [&value](char c) {
        const int d = c - '0';
        if (value > (std::numeric_limits<std::int64_t>::max() - d) / 10) { return false; }
        value = value * 10 + d;
        return true;
    };

    for (const char c : int_part) {
        if (!push(c)) { return std::nullopt; }
    }
    for (int i = 0; i < decimals; ++i) {
        const auto      at = static_cast<std::size_t>(i);
        const char      c  = (at < frac_part.size()) ? frac_part[at] : '0';
        if (!push(c)) { return std::nullopt; }
    }

    return negative ? -value : value;
}

}  // namespace harmoneyes::theia::detail
