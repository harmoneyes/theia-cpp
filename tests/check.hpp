#pragma once

#include <cstdio>
#include <string>

// A deliberately tiny assert helper. The test suite exists to guard a handful
// of contracts that are easy to break silently; adding a test framework as a
// dependency of a dependency-free SDK would cost more than it buys.

namespace hen_test {

inline int failures = 0;

inline void check(bool ok, const std::string& what)
{
    std::printf("  %s  %s\n", ok ? "pass" : "FAIL", what.c_str());
    if (!ok) { ++failures; }
}

inline int report(const char* suite)
{
    std::printf("%s: %s\n", suite, failures == 0 ? "all passed" : "FAILURES");
    return failures == 0 ? 0 : 1;
}

}  // namespace hen_test
