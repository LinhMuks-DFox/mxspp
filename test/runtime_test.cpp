// Unit tests for the runtime object model + dynamic dispatch (boxed MXObject + mxs_op_*).
// Pure C++ (no LLVM) — links against the compiled runtime.
#include "test_framework.h"

#include <cstdint>

extern "C" {
    struct MXObject;// opaque to the test; only manipulated via the C-ABI below
    MXObject *mxs_box_int(std::int64_t);
    MXObject *mxs_box_bool(std::int64_t);
    MXObject *mxs_box_float(double);
    std::int64_t mxs_obj_tag(const MXObject *);
    std::int64_t mxs_obj_as_int(const MXObject *);
    double mxs_obj_as_float(const MXObject *);
    std::int64_t mxs_obj_truthy(const MXObject *);
    void mxs_obj_free(MXObject *);
    MXObject *mxs_op_add(MXObject *, MXObject *);
    MXObject *mxs_op_sub(MXObject *, MXObject *);
    MXObject *mxs_op_mul(MXObject *, MXObject *);
}

namespace {
    constexpr std::int64_t TAG_INT = 0, TAG_FLOAT = 1, TAG_BOOL = 2;
}

MX_TEST(box_and_tags) {
    auto *i = mxs_box_int(42);
    CHECK(mxs_obj_tag(i) == TAG_INT);
    CHECK(mxs_obj_as_int(i) == 42);
    auto *f = mxs_box_float(3.5);
    CHECK(mxs_obj_tag(f) == TAG_FLOAT);
    CHECK(mxs_obj_as_float(f) == 3.5);
    auto *b = mxs_box_bool(7);
    CHECK(mxs_obj_tag(b) == TAG_BOOL);
    CHECK_MSG(mxs_obj_as_int(b) == 1, "bool is normalized to 0/1");
    mxs_obj_free(i);
    mxs_obj_free(f);
    mxs_obj_free(b);
}

MX_TEST(dynamic_dispatch_arithmetic) {
    // int op int stays int
    auto *s = mxs_op_add(mxs_box_int(2), mxs_box_int(3));
    CHECK(mxs_obj_tag(s) == TAG_INT);
    CHECK(mxs_obj_as_int(s) == 5);
    // any float promotes to float
    auto *p = mxs_op_add(mxs_box_int(2), mxs_box_float(1.5));
    CHECK(mxs_obj_tag(p) == TAG_FLOAT);
    CHECK(mxs_obj_as_float(p) == 3.5);
    // sub / mul
    CHECK(mxs_obj_as_int(mxs_op_sub(mxs_box_int(10), mxs_box_int(4))) == 6);
    CHECK(mxs_obj_as_int(mxs_op_mul(mxs_box_int(6), mxs_box_int(7))) == 42);
}

MX_TEST(truthiness) {
    CHECK(mxs_obj_truthy(mxs_box_int(0)) == 0);
    CHECK(mxs_obj_truthy(mxs_box_int(5)) == 1);
    CHECK(mxs_obj_truthy(mxs_box_bool(0)) == 0);
    CHECK(mxs_obj_truthy(mxs_box_float(0.0)) == 0);
    CHECK(mxs_obj_truthy(mxs_box_float(2.0)) == 1);
}

int main() { return mxtest::run_all(); }
