#pragma once
#include "mxspp/core/MXObject.h"

// std.time — timestamp primitives (progress17: relocated out of src/core into src/_std). Each
// returns a fresh, owned (+1) MXInteger. The language binds these via @@foreign in std/time.mxs.
extern "C" {
mxs::core::MXObject *mxs_time_now();// wall-clock seconds since the Unix epoch
mxs::core::MXObject *mxs_time_ms(); // wall-clock milliseconds since the Unix epoch
mxs::core::MXObject *mxs_time_ns(); // monotonic nanoseconds (for measuring durations)
}
