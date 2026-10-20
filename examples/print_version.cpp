// Smallest possible consumer: links the SDK and reports what it is running.
//
// Build:
//   cmake -B build && cmake --build build
//   ./build/examples/print_version

#include <harmoneyes/theia/version.hpp>

#include <iostream>

int main()
{
    namespace het = harmoneyes::theia;

    std::cout << "SDK package     : " << het::kVersion << '\n'
              << "native library  : " << het::version() << '\n'
              << "feature pipeline: " << het::feature_version() << '\n';

    return 0;
}
