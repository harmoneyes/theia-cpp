#pragma once

// The one place the vendored C header is included.
//
// What that buys, stated honestly: a single include site to audit, and no C
// type in a public function signature. It is NOT containment. Including any
// public header of this SDK pulls all of theia_export.h into the global
// namespace: every typedef, the error enum, and roughly 150 extern "C"
// declarations. A consumer can call theia_sdk_destroy() on a handle the RAII
// types own, and nothing stops them.
//
// One public type is a C type outright. GazeSample is an alias for
// gaze_sample_t, because the C struct already has exactly the right 52 fields
// and wrapping it would mean maintaining a 52-field converter for no gain.
// types.hpp static_asserts its layout so a vendored-header change breaks the
// build rather than silently changing what every field means.
//
// theia_export.h carries its own extern "C" guard, so none is needed here.

#include <harmoneyes/theia_export.h>
