#pragma once

#include <charconv>
#include <cstddef>

#include "harmoneyes/theia/detail/parse.hpp"
#include <optional>
#include <string>
#include <string_view>

// The webcam sidecar speaks newline-delimited JSON over stdio. Every message
// is a flat object with known keys, so this is a scanner for that shape rather
// than a JSON library: a public header must not drag a JSON dependency into
// every consumer's translation unit.
//
// It does have to be a real scanner, though. Searching for `"x":` with a plain
// substring match also matches inside string VALUES, so a message like
//   {"type":"error","msg":"bad \"x\": 3"}
// mis-parses. Everything below tracks string state and escapes.

namespace harmoneyes::theia::detail::sidecar {

/// Append `value` to `out` as a JSON string, including the quotes.
///
/// Required, not optional: the init message carries a filesystem path and a
/// licence blob. Every Windows path contains backslashes, and interpolating
/// one raw produces invalid JSON.
inline void append_json_string(std::string& out, std::string_view value)
{
    out += '"';
    for (const char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += kHex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

/// Decode a JSON string body (without the surrounding quotes).
[[nodiscard]] inline std::string unescape(std::string_view body)
{
    std::string out;
    out.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\' || i + 1 >= body.size()) { out += body[i]; continue; }
        switch (body[++i]) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                // Only the Basic Latin range is decoded; anything else is kept
                // verbatim rather than mangled, since these are diagnostics.
                if (i + 4 < body.size()) {
                    unsigned code = 0;
                    const auto* first = body.data() + i + 1;
                    if (std::from_chars(first, first + 4, code, 16).ec == std::errc{} && code < 0x80) {
                        out += static_cast<char>(code);
                        i += 4;
                        break;
                    }
                }
                out += "\\u";
                break;
            }
            default: out += '\\'; out += body[i];
        }
    }
    return out;
}

/// The raw text of `key`'s value in a flat JSON object, if present.
///
/// Only looks at the top level, and ignores anything inside a string.
[[nodiscard]] inline std::optional<std::string_view> raw_value(std::string_view line,
                                                               std::string_view key)
{
    bool        in_string = false;
    int         depth     = 0;
    std::size_t i         = 0;

    while (i < line.size()) {
        const char c = line[i];

        if (in_string) {
            if (c == '\\') { i += 2; continue; }
            if (c == '"')  { in_string = false; }
            ++i;
            continue;
        }

        if (c == '"') {
            // A string at depth 1 followed by ':' is a key.
            const std::size_t start = i + 1;
            std::size_t       end   = start;
            while (end < line.size()) {
                if (line[end] == '\\') { end += 2; continue; }
                if (line[end] == '"')  { break; }
                ++end;
            }
            if (end >= line.size()) { return std::nullopt; }

            std::size_t after = end + 1;
            while (after < line.size() && (line[after] == ' ' || line[after] == '\t')) { ++after; }

            const bool is_key = after < line.size() && line[after] == ':';
            if (is_key && depth == 1 && line.substr(start, end - start) == key) {
                std::size_t vs = after + 1;
                while (vs < line.size() && (line[vs] == ' ' || line[vs] == '\t')) { ++vs; }
                // Find the end of this value, honouring nesting and strings.
                std::size_t ve        = vs;
                int         vdepth    = 0;
                bool        vin_string = false;
                while (ve < line.size()) {
                    const char d = line[ve];
                    if (vin_string) {
                        if (d == '\\') { ve += 2; continue; }
                        if (d == '"')  { vin_string = false; }
                    } else if (d == '"') {
                        vin_string = true;
                    } else if (d == '{' || d == '[') {
                        ++vdepth;
                    } else if (d == '}' || d == ']') {
                        if (vdepth == 0) { break; }
                        --vdepth;
                    } else if (d == ',' && vdepth == 0) {
                        break;
                    }
                    ++ve;
                }
                // Trim trailing space so `{"valid": true }` parses. JSON.stringify
                // never emits it, but a hand-written producer or stub will.
                while (ve > vs && (line[ve - 1] == ' ' || line[ve - 1] == '\t')) { --ve; }
                return line.substr(vs, ve - vs);
            }

            if (is_key) { i = after + 1; } else { i = end + 1; }
            continue;
        }

        if (c == '{' || c == '[') { ++depth; }
        else if (c == '}' || c == ']') { --depth; }
        ++i;
    }
    return std::nullopt;
}

/// A string-valued field, unescaped.
[[nodiscard]] inline std::optional<std::string> string_field(std::string_view line,
                                                             std::string_view key)
{
    const auto raw = raw_value(line, key);
    if (!raw || raw->size() < 2 || raw->front() != '"' || raw->back() != '"') { return std::nullopt; }
    return unescape(raw->substr(1, raw->size() - 2));
}

/// A number-valued field.
///
/// Goes through detail::parse_double, which is locale-independent (the wire
/// format is always C-locale) and works on standard libraries whose
/// std::from_chars does not yet cover floating point.
[[nodiscard]] inline std::optional<double> number_field(std::string_view line,
                                                        std::string_view key)
{
    const auto raw = raw_value(line, key);
    if (!raw) { return std::nullopt; }
    return parse_double(*raw);
}

/// A boolean-valued field.
[[nodiscard]] inline std::optional<bool> bool_field(std::string_view line, std::string_view key)
{
    const auto raw = raw_value(line, key);
    if (!raw) { return std::nullopt; }
    if (*raw == "true")  { return true; }
    if (*raw == "false") { return false; }
    return std::nullopt;
}

}  // namespace harmoneyes::theia::detail::sidecar
