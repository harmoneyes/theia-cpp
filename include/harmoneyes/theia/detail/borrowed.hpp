#pragma once

#include <string>

// Every `const char*` the C ABI hands back points into storage the library
// owns, usually a thread-local buffer that the next call overwrites. Nothing
// here is ever freed by this SDK; there is no theia_free().
//
// The rule is therefore: copy at the boundary, always, before returning to the
// caller. Never store or return a borrowed pointer.

namespace harmoneyes::theia::detail {

/// Copy a borrowed C string. A null pointer becomes an empty string.
[[nodiscard]] inline std::string copy_borrowed(const char* s)
{
    return s ? std::string{s} : std::string{};
}

}  // namespace harmoneyes::theia::detail
