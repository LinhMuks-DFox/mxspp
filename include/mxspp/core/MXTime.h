#pragma once
#include "mxspp/core/MXObject.h"

// time — timestamp primitives (progress13). Each returns a fresh, owned (+1) MXInteger. These are
// the leaves the language's `time` facility binds to via @@foreign.
extern "C" {
mxs::core::MXObject *mxs_time_now();// wall-clock seconds since the Unix epoch
mxs::core::MXObject *mxs_time_ms(); // wall-clock milliseconds since the Unix epoch
mxs::core::MXObject *mxs_time_ns(); // monotonic nanoseconds (for measuring durations)
}
