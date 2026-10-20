// The Tobii engine licence arrives as UTF-16LE. Decoding it wrong yields a
// string that looks plausible and is rejected by the engine.

#include <harmoneyes/theia/nexus_webcam.hpp>
#include "check.hpp"

namespace hn = harmoneyes::theia::nexus;
using hen_test::check;

static std::vector<unsigned char> utf16le(const std::string& text, bool bom)
{
    std::vector<unsigned char> out;
    if (bom) { out.push_back(0xFF); out.push_back(0xFE); }
    for (const char c : text) { out.push_back(static_cast<unsigned char>(c)); out.push_back(0); }
    return out;
}

int main()
{
    check(hn::TobiiLicense::from_utf16le(utf16le("abc123", false)).text() == "abc123", "plain");
    check(hn::TobiiLicense::from_utf16le(utf16le("abc123", true)).text() == "abc123", "BOM stripped");
    check(hn::TobiiLicense::from_utf16le(utf16le("  abc  ", false)).text() == "abc", "trimmed");

    bool threw = false;
    try { hn::TobiiLicense::from_utf16le({0x41}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "an odd byte count is rejected");

    threw = false;
    try { hn::TobiiLicense::from_utf16le({0x41, 0x01}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "non Basic-Latin input is rejected rather than mangled");

    check(hn::TobiiLicense::from_string("xyz").text() == "xyz", "from_string");
    check(hn::TobiiLicense{}.empty(), "default is empty");

    return hen_test::report("license");
}
