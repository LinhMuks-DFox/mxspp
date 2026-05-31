// Unit tests for the runtime object model + dynamic dispatch (boxed MXObject + mxs_op_*).
// Pure C++ (no LLVM) — links against the compiled runtime. Panic paths (which call
// mxs_panic -> exit) are exercised with fork-based death tests so coverage stays complete.
#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
struct MXObject;// opaque to the test; only manipulated via the C-ABI below
MXObject *mxs_box_int(std::int64_t);
MXObject *mxs_box_bool(std::int64_t);
MXObject *mxs_box_float(double);
MXObject *mxs_box_nil();
MXObject *mxs_box_str(const char *);
std::int64_t mxs_obj_tag(const MXObject *);
std::int64_t mxs_obj_as_int(const MXObject *);
double mxs_obj_as_float(const MXObject *);
std::int64_t mxs_obj_truthy(const MXObject *);
void mxs_obj_free(MXObject *);
MXObject *mxs_op_add(MXObject *, MXObject *);
MXObject *mxs_op_sub(MXObject *, MXObject *);
MXObject *mxs_op_mul(MXObject *, MXObject *);
MXObject *mxs_op_div(MXObject *, MXObject *);
MXObject *mxs_op_mod(MXObject *, MXObject *);
MXObject *mxs_op_neg(MXObject *);
MXObject *mxs_op_not(MXObject *);
MXObject *mxs_op_lt(MXObject *, MXObject *);
MXObject *mxs_op_le(MXObject *, MXObject *);
MXObject *mxs_op_gt(MXObject *, MXObject *);
MXObject *mxs_op_ge(MXObject *, MXObject *);
MXObject *mxs_op_eq(MXObject *, MXObject *);
MXObject *mxs_op_ne(MXObject *, MXObject *);
}

namespace {
    constexpr std::int64_t TAG_INT = 0, TAG_FLOAT = 1, TAG_BOOL = 2, TAG_STR = 3,
                           TAG_NIL = 4;

    // Run `fn` in a forked child; true iff it terminated the process via exit(1) (a panic).
    // stderr is closed in the child so the panic message doesn't clutter test output.
    template<class F>
    bool dies(F &&fn) {
        const pid_t pid = fork();
        if (pid == 0) {
            std::fclose(stderr);
            fn();
            _exit(0);// fn returned without panicking
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 1;
    }
}// namespace

MX_TEST(box_and_tags) {
    auto *i = mxs_box_int(42);
    CHECK(mxs_obj_tag(i) == TAG_INT);
    CHECK(mxs_obj_as_int(i) == 42);
    auto *f = mxs_box_float(3.5);
    CHECK(mxs_obj_tag(f) == TAG_FLOAT);
    CHECK(mxs_obj_as_float(f) == 3.5);
    CHECK_MSG(mxs_obj_as_int(f) == 3, "float -> int truncates");
    auto *b = mxs_box_bool(7);
    CHECK(mxs_obj_tag(b) == TAG_BOOL);
    CHECK_MSG(mxs_obj_as_int(b) == 1, "bool is normalized to 0/1");
    auto *n = mxs_box_nil();
    CHECK(mxs_obj_tag(n) == TAG_NIL);
    auto *s = mxs_box_str("hi");
    CHECK(mxs_obj_tag(s) == TAG_STR);
    auto *empty = mxs_box_str(nullptr);// null -> empty string, not a crash
    CHECK(mxs_obj_tag(empty) == TAG_STR);
    mxs_obj_free(i);
    mxs_obj_free(f);
    mxs_obj_free(b);
    mxs_obj_free(n);
    mxs_obj_free(s);
    mxs_obj_free(empty);
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
    // sub / mul (int + float-promoting)
    CHECK(mxs_obj_as_int(mxs_op_sub(mxs_box_int(10), mxs_box_int(4))) == 6);
    CHECK(mxs_obj_as_int(mxs_op_mul(mxs_box_int(6), mxs_box_int(7))) == 42);
    CHECK(mxs_obj_as_float(mxs_op_sub(mxs_box_float(2.5), mxs_box_int(1))) == 1.5);
    CHECK(mxs_obj_as_float(mxs_op_mul(mxs_box_int(2), mxs_box_float(2.5))) == 5.0);
}

MX_TEST(division_and_modulo) {
    auto *d = mxs_op_div(mxs_box_int(20), mxs_box_int(5));
    CHECK(mxs_obj_tag(d) == TAG_INT);
    CHECK(mxs_obj_as_int(d) == 4);
    CHECK(mxs_obj_as_int(mxs_op_mod(mxs_box_int(20), mxs_box_int(7))) == 6);
    // float-promoting div / mod
    CHECK(mxs_obj_as_float(mxs_op_div(mxs_box_int(7), mxs_box_float(2.0))) == 3.5);
    CHECK(mxs_obj_as_float(mxs_op_mod(mxs_box_float(7.5), mxs_box_int(2))) == 1.5);
    // zero divisor / modulus panics
    CHECK_MSG(dies([] { mxs_op_div(mxs_box_int(1), mxs_box_int(0)); }),
              "int div by zero");
    CHECK_MSG(dies([] { mxs_op_mod(mxs_box_int(1), mxs_box_int(0)); }),
              "int mod by zero");
    CHECK_MSG(dies([] { mxs_op_div(mxs_box_int(1), mxs_box_float(0.0)); }),
              "float div by zero");
    CHECK_MSG(dies([] { mxs_op_mod(mxs_box_int(1), mxs_box_float(0.0)); }),
              "float mod by zero");
}

MX_TEST(unary_neg_not) {
    CHECK(mxs_obj_as_int(mxs_op_neg(mxs_box_int(5))) == -5);
    CHECK(mxs_obj_as_float(mxs_op_neg(mxs_box_float(2.5))) == -2.5);
    CHECK(mxs_obj_as_int(mxs_op_neg(mxs_box_bool(1))) == -1);
    // negating a non-number panics
    CHECK_MSG(dies([] { mxs_op_neg(mxs_box_str("x")); }), "neg string panics");
    CHECK_MSG(dies([] { mxs_op_neg(mxs_box_nil()); }), "neg nil panics");
    // logical not always yields a boxed bool, never panics
    auto *t = mxs_op_not(mxs_box_int(0));
    CHECK(mxs_obj_tag(t) == TAG_BOOL);
    CHECK(mxs_obj_as_int(t) == 1);
    CHECK(mxs_obj_as_int(mxs_op_not(mxs_box_int(5))) == 0);
    CHECK(mxs_obj_as_int(mxs_op_not(mxs_box_str(""))) == 1);
    CHECK(mxs_obj_as_int(mxs_op_not(mxs_box_nil())) == 1);
}

MX_TEST(comparisons_numeric) {
    auto *lt = mxs_op_lt(mxs_box_int(2), mxs_box_int(3));
    CHECK_MSG(mxs_obj_tag(lt) == TAG_BOOL, "comparison yields a boxed bool");
    CHECK(mxs_obj_truthy(lt) == 1);
    CHECK(mxs_obj_truthy(mxs_op_lt(mxs_box_int(3), mxs_box_int(3))) == 0);
    CHECK(mxs_obj_truthy(mxs_op_le(mxs_box_int(3), mxs_box_int(3))) == 1);
    CHECK(mxs_obj_truthy(mxs_op_gt(mxs_box_int(4), mxs_box_int(3))) == 1);
    CHECK(mxs_obj_truthy(mxs_op_ge(mxs_box_int(3), mxs_box_int(4))) == 0);
    // cross-type numeric comparison (int vs float)
    CHECK(mxs_obj_truthy(mxs_op_lt(mxs_box_int(2), mxs_box_float(2.5))) == 1);
    // equality across int/float, and ne as its negation
    CHECK(mxs_obj_truthy(mxs_op_eq(mxs_box_int(2), mxs_box_float(2.0))) == 1);
    CHECK(mxs_obj_truthy(mxs_op_ne(mxs_box_int(2), mxs_box_float(2.0))) == 0);
    CHECK(mxs_obj_truthy(mxs_op_eq(mxs_box_int(2), mxs_box_int(3))) == 0);
}

MX_TEST(strings) {
    // truthiness: empty is false, non-empty is true
    CHECK(mxs_obj_truthy(mxs_box_str("")) == 0);
    CHECK(mxs_obj_truthy(mxs_box_str("x")) == 1);
    // concatenation via +
    auto *cat = mxs_op_add(mxs_box_str("foo"), mxs_box_str("bar"));
    CHECK(mxs_obj_tag(cat) == TAG_STR);
    CHECK_MSG(mxs_obj_truthy(mxs_op_eq(cat, mxs_box_str("foobar"))) == 1,
              "foo+bar==foobar");
    // equality / inequality
    CHECK(mxs_obj_truthy(mxs_op_eq(mxs_box_str("a"), mxs_box_str("a"))) == 1);
    CHECK(mxs_obj_truthy(mxs_op_eq(mxs_box_str("a"), mxs_box_str("b"))) == 0);
    CHECK(mxs_obj_truthy(mxs_op_ne(mxs_box_str("a"), mxs_box_str("b"))) == 1);
    // string vs number equality is false (not a panic)
    CHECK(mxs_obj_truthy(mxs_op_eq(mxs_box_str("1"), mxs_box_int(1))) == 0);
    // lexicographic ordering
    CHECK(mxs_obj_truthy(mxs_op_lt(mxs_box_str("abc"), mxs_box_str("abd"))) == 1);
    CHECK(mxs_obj_truthy(mxs_op_gt(mxs_box_str("b"), mxs_box_str("a"))) == 1);
    CHECK(mxs_obj_truthy(mxs_op_ge(mxs_box_str("a"), mxs_box_str("a"))) == 1);
    // mixing string + number, or ordering string vs number, panics
    CHECK_MSG(dies([] { mxs_op_add(mxs_box_str("a"), mxs_box_int(1)); }),
              "str + num panics");
    CHECK_MSG(dies([] { mxs_op_lt(mxs_box_str("a"), mxs_box_int(1)); }),
              "str < num panics");
}

MX_TEST(truthiness) {
    CHECK(mxs_obj_truthy(mxs_box_int(0)) == 0);
    CHECK(mxs_obj_truthy(mxs_box_int(5)) == 1);
    CHECK(mxs_obj_truthy(mxs_box_bool(0)) == 0);
    CHECK(mxs_obj_truthy(mxs_box_float(0.0)) == 0);
    CHECK(mxs_obj_truthy(mxs_box_float(2.0)) == 1);
    CHECK(mxs_obj_truthy(mxs_box_nil()) == 0);
}

int main() { return mxtest::run_all(); }
