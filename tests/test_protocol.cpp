// The sidecar speaks newline-delimited JSON. A substring search for a key also
// matches inside string VALUES, which is how the existing C blueprint
// mis-parses an error message that happens to contain one.

#include <harmoneyes/theia/detail/sidecar_protocol.hpp>
#include "check.hpp"

namespace sc = harmoneyes::theia::detail::sidecar;
using hen_test::check;

int main()
{
    const std::string trap = R"({"type":"error","msg":"bad \"x\": 3"})";
    check(sc::string_field(trap, "type") == "error", "key found past an escaped quote");
    check(!sc::number_field(trap, "x").has_value(), "no spurious match inside a string value");
    check(sc::string_field(trap, "msg") == "bad \"x\": 3", "escapes decoded");

    const std::string gaze = R"({"type":"gaze","x":0.25,"y":0.75,"valid":true,"timestamp":1234.5})";
    check(sc::number_field(gaze, "x") == 0.25, "number");
    check(sc::bool_field(gaze, "valid") == true, "boolean");
    check(sc::number_field(gaze, "timestamp") == 1234.5, "fractional number");
    check(sc::number_field(R"({"x":1.5e-3})", "x") == 1.5e-3, "exponent form");

    const std::string nested = R"({"type":"fps","res":{"w":640,"h":480},"fps":30})";
    check(sc::number_field(nested, "fps") == 30, "key after a nested object");
    check(!sc::number_field(nested, "w").has_value(), "nested keys stay nested");

    // Windows paths are the reason escaping is mandatory, not optional.
    std::string out;
    sc::append_json_string(out, R"(C:\sidecar\a "b")");
    const std::string line = R"({"p":)" + out + "}";
    check(sc::string_field(line, "p") == R"(C:\sidecar\a "b")", "windows path round-trips");

    return hen_test::report("protocol");
}
