#include "mxspp/_std/time.h"

#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXObject.h"

#include <chrono>
#include <cstdint>

// ============================================================================================
// std.time — timestamp primitives (progress17: relocated from src/core/MXTime.cpp). Wall-clock
// readings use system_clock; the monotonic reading uses steady_clock (the right source for
// measuring elapsed time). All return MXInteger.
// ============================================================================================
extern "C" {

mxs::core::MXObject *mxs_time_now() {
    using namespace std::chrono;
    const auto s = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    return new mxs::builtin::MXInteger(static_cast<std::int64_t>(s));
}

mxs::core::MXObject *mxs_time_ms() {
    using namespace std::chrono;
    const auto ms =
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return new mxs::builtin::MXInteger(static_cast<std::int64_t>(ms));
}

mxs::core::MXObject *mxs_time_ns() {
    using namespace std::chrono;
    const auto ns =
            duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return new mxs::builtin::MXInteger(static_cast<std::int64_t>(ns));
}
}
