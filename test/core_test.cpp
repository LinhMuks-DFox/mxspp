// Unit tests for the core C++ object types (real MXObject subclasses). Links `core`.
// Currently: MXInteger (arbitrary-precision integer, progress09).
#include "test_framework.h"

#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXBoolean.h"
#include "mxspp/core/MXClassInfo.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXInstance.h"
#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXLeftValue.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXPopulationManager.h"
#include "mxspp/core/MXString.h"

#include <cstdint>
#include <memory>
#include <string>

extern "C" {
void mxs_retain(const mxs::core::MXObject *);
void mxs_release(const mxs::core::MXObject *);
// Dynamic-dispatch operators (MXOps.cpp).
mxs::core::MXObject *mxs_op_add(mxs::core::MXObject *, mxs::core::MXObject *);
mxs::core::MXObject *mxs_op_div(mxs::core::MXObject *, mxs::core::MXObject *);
mxs::core::MXObject *mxs_op_lt(mxs::core::MXObject *, mxs::core::MXObject *);
mxs::core::MXObject *mxs_op_eq(mxs::core::MXObject *, mxs::core::MXObject *);
// User class instance ABI (MXOps.cpp / MXInstance.cpp, progress11).
mxs::core::MXObject *mxs_instance_new(const mxs::core::MXClassInfo *);
void mxs_set_attr(mxs::core::MXObject *, const char *, mxs::core::MXObject *);
mxs::core::MXObject *mxs_get_attr(const mxs::core::MXObject *, const char *);
std::int64_t mxs_is_type(const mxs::core::MXObject *, const char *);
const mxs::core::MXClassInfo *mxs_object_classinfo(const mxs::core::MXObject *);
// stdio: str/repr builtins + format (src/_std/builtins.cpp, string.cpp) + list packing (MXArrayList.cpp).
mxs::core::MXObject *mxs_str(mxs::core::MXObject *);
mxs::core::MXObject *mxs_repr(mxs::core::MXObject *);
mxs::core::MXObject *mxs_format(mxs::core::MXObject *, mxs::core::MXObject *);
mxs::core::MXObject *mxs_arraylist_new();
void mxs_arraylist_append(mxs::core::MXObject *, mxs::core::MXObject *);
}

using mxs::make_immutable_left_value;
using mxs::make_mutable_left_value;
using mxs::MXObjectOwned;
using mxs::builtin::MXArrayList;
using mxs::builtin::MXBoolean;
using mxs::builtin::MXFloat;
using mxs::builtin::MXInteger;
using mxs::builtin::MXNil;
using mxs::builtin::MXString;
using mxs::core::MXObject;

namespace {
    // Decimal string of an arithmetic result, or "<err:Name>" if it produced an MXError.
    std::string dec(const MXObjectOwned &o) {
        if (auto *i = dynamic_cast<const MXInteger *>(o.get())) return i->to_decimal();
        return "<err>";
    }
    MXObjectOwned lit(const std::string &s) { return MXInteger::from_literal(s); }
    const MXInteger &as_int(const MXObjectOwned &o) {
        return *dynamic_cast<const MXInteger *>(o.get());
    }
}

MX_TEST(integer_from_literal_and_repr) {
    CHECK(as_int(lit("0")).to_decimal() == "0");
    CHECK(as_int(lit("42")).to_decimal() == "42");
    CHECK(as_int(lit("-42")).to_decimal() == "-42");
    CHECK(as_int(lit("+7")).to_decimal() == "7");
    // a value well beyond 64 bits round-trips through decimal exactly
    const std::string big = "123456789012345678901234567890123456789";
    CHECK(as_int(lit(big)).to_decimal() == big);
    CHECK(as_int(lit("0")).repr() == "0");
    // bad literals -> MXError
    CHECK(dynamic_cast<const mxs::core::MXError *>(lit("12x3").get()) != nullptr);
    CHECK(dynamic_cast<const mxs::core::MXError *>(lit("-").get()) != nullptr);
}

MX_TEST(integer_add_sub_mul) {
    CHECK(dec(as_int(lit("2")).add(as_int(lit("3")))) == "5");
    CHECK(dec(as_int(lit("-2")).add(as_int(lit("3")))) == "1");
    CHECK(dec(as_int(lit("2")).add(as_int(lit("-3")))) == "-1");
    CHECK(dec(as_int(lit("2")).add(as_int(lit("-2")))) == "0");
    CHECK(dec(as_int(lit("10")).sub(as_int(lit("4")))) == "6");
    CHECK(dec(as_int(lit("4")).sub(as_int(lit("10")))) == "-6");
    CHECK(dec(as_int(lit("6")).mul(as_int(lit("7")))) == "42");
    CHECK(dec(as_int(lit("-6")).mul(as_int(lit("7")))) == "-42");
    CHECK(dec(as_int(lit("-6")).mul(as_int(lit("-7")))) == "42");
    // carry across a 64-bit limb boundary: 2^64 = 18446744073709551616
    CHECK(dec(as_int(lit("18446744073709551615")).add(as_int(lit("1")))) ==
          "18446744073709551616");
}

MX_TEST(integer_div_mod) {
    CHECK(dec(as_int(lit("20")).div(as_int(lit("5")))) == "4");
    CHECK(dec(as_int(lit("20")).div(as_int(lit("7")))) == "2");// truncates
    CHECK(dec(as_int(lit("20")).mod(as_int(lit("7")))) == "6");
    CHECK(dec(as_int(lit("-20")).div(as_int(lit("7")))) == "-2");
    CHECK(dec(as_int(lit("-20")).mod(as_int(lit("7")))) == "-6");// sign of dividend
    // division by zero -> MXError, not a crash
    CHECK(dynamic_cast<const mxs::core::MXError *>(
                  as_int(lit("1")).div(as_int(lit("0"))).get()) != nullptr);
    CHECK(dynamic_cast<const mxs::core::MXError *>(
                  as_int(lit("1")).mod(as_int(lit("0"))).get()) != nullptr);
    // big / big: (10^30) / (10^15) == 10^15
    CHECK(dec(as_int(lit("1000000000000000000000000000000"))
                      .div(as_int(lit("1000000000000000")))) == "1000000000000000");
}

MX_TEST(integer_pow_bignum) {
    CHECK(dec(as_int(lit("2")).pow(as_int(lit("10")))) == "1024");
    CHECK(dec(as_int(lit("-2")).pow(as_int(lit("3")))) == "-8");
    CHECK(dec(as_int(lit("-2")).pow(as_int(lit("4")))) == "16");
    CHECK(dec(as_int(lit("7")).pow(as_int(lit("0")))) == "1");
    // 2 ** 256 — the headline big-number case (progress09 D4)
    const std::string two256 = "115792089237316195423570985008687907853269984665640564039"
                               "457584007913129639936";
    CHECK(dec(as_int(lit("2")).pow(as_int(lit("256")))) == two256);
    // negative exponent -> MXError
    CHECK(dynamic_cast<const mxs::core::MXError *>(
                  as_int(lit("2")).pow(as_int(lit("-1"))).get()) != nullptr);
}

MX_TEST(integer_cmp) {
    CHECK(as_int(lit("2")).cmp(as_int(lit("3"))) < 0);
    CHECK(as_int(lit("3")).cmp(as_int(lit("3"))) == 0);
    CHECK(as_int(lit("4")).cmp(as_int(lit("3"))) > 0);
    CHECK(as_int(lit("-4")).cmp(as_int(lit("3"))) < 0);
    CHECK(as_int(lit("-4")).cmp(as_int(lit("-3"))) < 0);// -4 < -3
    CHECK(as_int(lit("-3")).cmp(as_int(lit("-4"))) > 0);
}

MX_TEST(integer_int_type) {
    CHECK(as_int(lit("0")).int_type() == "int8");
    CHECK(as_int(lit("127")).int_type() == "int8");
    CHECK(as_int(lit("128")).int_type() == "uint8");
    CHECK(as_int(lit("255")).int_type() == "uint8");
    CHECK(as_int(lit("256")).int_type() == "int16");
    CHECK(as_int(lit("-128")).int_type() == "int8");
    CHECK(as_int(lit("-129")).int_type() == "int16");
    CHECK(as_int(lit("4294967295")).int_type() == "uint32");// 2^32 - 1
    CHECK(as_int(lit("9223372036854775807")).int_type() == "int64");// INT64_MAX
    CHECK(as_int(lit("18446744073709551615")).int_type() == "uint64");// 2^64 - 1
    CHECK(as_int(lit("18446744073709551616")).int_type() == "UltraInteger");// 2^64
    // 2 ** 256 is firmly UltraInteger
    CHECK(dynamic_cast<const MXInteger &>(*as_int(lit("2")).pow(as_int(lit("256"))))
                  .int_type() == "UltraInteger");
}

MX_TEST(integer_rtti_and_size) {
    auto i = lit("5");
    CHECK(std::string(i->repr()) == "5");
    CHECK(&MXInteger::get_rtti() != nullptr);
    CHECK(std::string(MXInteger::get_rtti().name) == "MXInteger");
    CHECK(MXInteger::get_rtti().parent == &MXObject::get_rtti());
    // int_size is at least the object header (storage may be 0 for the small value 5's 1 limb)
    CHECK(as_int(i).int_size() >= sizeof(MXInteger));
}

MX_TEST(string_basics) {
    MXString hello("hello");
    MXString world(" world");
    // concat
    auto cat = hello.concat(world);
    CHECK(dynamic_cast<const MXString *>(cat.get())->value() == "hello world");
    CHECK(hello.value() == "hello");// operands unchanged (immutable)
    // length -> MXInteger
    auto len = hello.length();
    CHECK(dynamic_cast<const MXInteger *>(len.get())->to_decimal() == "5");
    CHECK(dynamic_cast<const MXInteger *>(MXString("").length().get())->to_decimal() ==
          "0");
    // lexicographic compare
    CHECK(MXString("abc").cmp(MXString("abd")) < 0);
    CHECK(MXString("abc").cmp(MXString("abc")) == 0);
    CHECK(MXString("b").cmp(MXString("a")) > 0);
    // str() is the raw bytes; repr() is the quoted form (progress12 D-STR-REPR). from_literal
    // builds an MXString.
    CHECK(MXString("hi").str() == "hi");
    CHECK(MXString("hi").repr() == "\"hi\"");
    CHECK(dynamic_cast<const MXString *>(MXString::from_literal("lit").get())->value() ==
          "lit");
    CHECK(MXString("").empty());
    CHECK(!MXString("x").empty());
}

MX_TEST(string_equals_and_rtti) {
    MXString a("same"), b("same"), c("diff");
    MXObjectOwned bo = std::make_unique<MXString>("same");
    MXObjectOwned co = std::make_unique<MXString>("diff");
    CHECK(a.equals(bo));
    CHECK(!a.equals(co));
    // a string never equals a non-string
    MXObjectOwned i = MXInteger::from_literal("5");
    CHECK(!a.equals(i));
    CHECK(std::string(MXString::get_rtti().name) == "MXString");
    CHECK(MXString::get_rtti().parent == &MXObject::get_rtti());
    // equal strings hash equal; different strings (almost surely) don't
    CHECK(a.get_hash_code() == b.get_hash_code());
    CHECK(a.get_hash_code() != c.get_hash_code());
}

MX_TEST(float_basics) {
    auto *f = dynamic_cast<const MXFloat *>(MXFloat::from_literal("3.5").get());
    CHECK(f != nullptr);
    CHECK(MXFloat(2.0).add(MXFloat(0.5))->repr() == "2.5");
    CHECK(dynamic_cast<const MXFloat *>(MXFloat(2.5).sub(MXFloat(1.0)).get())->value() ==
          1.5);
    CHECK(dynamic_cast<const MXFloat *>(MXFloat(2.0).mul(MXFloat(2.5)).get())->value() ==
          5.0);
    CHECK(dynamic_cast<const MXFloat *>(MXFloat(7.0).div(MXFloat(2.0)).get())->value() ==
          3.5);
    CHECK(dynamic_cast<const MXFloat *>(MXFloat(3.0).neg().get())->value() == -3.0);
    CHECK(MXFloat(1.0).cmp(MXFloat(2.0)) < 0);
    CHECK(MXFloat(2.0).cmp(MXFloat(2.0)) == 0);
    CHECK(MXFloat(3.0).cmp(MXFloat(2.0)) > 0);
    // division by zero -> MXError; bad literal -> MXError
    CHECK(dynamic_cast<const mxs::core::MXError *>(
                  MXFloat(1.0).div(MXFloat(0.0)).get()) != nullptr);
    CHECK(dynamic_cast<const mxs::core::MXError *>(MXFloat::from_literal("x").get()) !=
          nullptr);
    CHECK(std::string(MXFloat::get_rtti().name) == "MXFloat");
    CHECK(MXFloat::get_rtti().parent == &MXObject::get_rtti());
}

MX_TEST(boolean_basics) {
    CHECK(MXBoolean(true).repr() == "true");
    CHECK(MXBoolean(false).repr() == "false");
    CHECK(MXBoolean(true).value());
    CHECK(dynamic_cast<const MXBoolean *>(MXBoolean(true).logical_not().get())->value() ==
          false);
    MXObjectOwned t1 = std::make_unique<MXBoolean>(true);
    CHECK(MXBoolean(true).equals(t1));
    MXObjectOwned f1 = std::make_unique<MXBoolean>(false);
    CHECK(!MXBoolean(true).equals(f1));
    CHECK(std::string(MXBoolean::get_rtti().name) == "MXBoolean");
    CHECK(MXBoolean::get_rtti().parent == &MXObject::get_rtti());
}

MX_TEST(nil_basics) {
    MXNil n;
    CHECK(n.repr() == "nil");
    MXObjectOwned other = std::make_unique<MXNil>();
    CHECK(n.equals(other));// all nils are equal
    MXObjectOwned anInt = MXInteger::from_literal("0");
    CHECK(!n.equals(anInt));// nil is not equal to a (falsey) integer
    CHECK(std::string(MXNil::get_rtti().name) == "MXNil");
    CHECK(MXNil::get_rtti().parent == &MXObject::get_rtti());
}

MX_TEST(arraylist_basics) {
    // The list OWNS its elements via refcounting (ARC, progress11): append/set retain, and the
    // list releases them on destruction. So we hand it freshly-created (+1) objects and drop our
    // own reference, leaving the list as the sole owner (no unique_ptr double-management).
    MXArrayList xs;
    CHECK(dynamic_cast<const MXInteger *>(xs.length().get())->to_decimal() == "0");
    CHECK(xs.repr() == "[]");
    auto *a = MXInteger::from_literal("10").release();// raw +1
    auto *b = MXInteger::from_literal("20").release();
    auto *c = MXInteger::from_literal("30").release();
    xs.append(a);// list retains -> +2; drop our +1 -> list is sole owner (+1)
    a->release();
    xs.append(b);
    b->release();
    xs.append(c);
    c->release();
    CHECK(xs.size() == 3);
    CHECK(dynamic_cast<const MXInteger *>(xs.length().get())->to_decimal() == "3");
    CHECK(xs.repr() == "[10, 20, 30]");
    CHECK(xs.get(0) == a);
    CHECK(dynamic_cast<const MXInteger *>(xs.get(2))->to_decimal() == "30");
    CHECK(xs.get(3) == nullptr);// out of range -> nullptr (C++ API)
    CHECK(xs.get(-1) == nullptr);
    // set: retains the new element, releases the old one (b is freed here).
    auto *d = MXInteger::from_literal("99").release();
    CHECK(xs.set(1, d));
    d->release();// list is the sole owner of d
    CHECK(xs.repr() == "[10, 99, 30]");
    CHECK(!xs.set(5, d));// out of range; d untouched (still owned by the list at index 1)
    // concat -> a new list that retains the shared elements.
    MXArrayList ys;
    auto *e = MXInteger::from_literal("40").release();
    ys.append(e);
    e->release();
    auto z = xs.concat(ys);
    CHECK(dynamic_cast<const MXArrayList *>(z.get())->size() == 4);
    CHECK(dynamic_cast<const MXArrayList *>(z.get())->repr() == "[10, 99, 30, 40]");
    CHECK(xs.size() == 3);// operands unchanged
    // RTTI
    CHECK(std::string(MXArrayList::get_rtti().name) == "MXArrayList");
    CHECK(MXArrayList::get_rtti().parent == &MXObject::get_rtti());
}

MX_TEST(left_value_immutable_and_mutable) {
    // `let x = 3;`  ->  an immutable binding holding an MXInteger
    auto x = make_immutable_left_value(MXInteger::from_literal("3"));
    CHECK(!x->is_mutable());
    CHECK(dynamic_cast<const MXInteger *>(x->rvalue())->to_decimal() == "3");
    // `x += 3;`  ->  rvalue_update on an immutable binding is an error; value is unchanged
    auto *cur = dynamic_cast<const MXInteger *>(x->rvalue());
    auto sum = cur->add(
            *dynamic_cast<const MXInteger *>(MXInteger::from_literal("3").get()));
    auto res = x->rvalue_update(std::move(sum));
    CHECK_MSG(dynamic_cast<const mxs::core::MXError *>(res.get()) != nullptr,
              "immutable binding rejects update");
    CHECK(dynamic_cast<const MXInteger *>(x->rvalue())->to_decimal() == "3");// unchanged

    // `let mut y = 3; y += 3;`  ->  mutable binding updates successfully
    auto y = make_mutable_left_value(MXInteger::from_literal("3"));
    CHECK(y->is_mutable());
    auto *yc = dynamic_cast<const MXInteger *>(y->rvalue());
    auto ysum =
            yc->add(*dynamic_cast<const MXInteger *>(MXInteger::from_literal("3").get()));
    auto ok = y->rvalue_update(std::move(ysum));
    CHECK_MSG(dynamic_cast<const mxs::builtin::MXNil *>(ok.get()) != nullptr,
              "mutable update returns nil");
    CHECK(dynamic_cast<const MXInteger *>(y->rvalue())->to_decimal() == "6");
}

MX_TEST(refcounting) {
    // A freshly-created object starts with one reference (the creator's).
    auto *o = new MXInteger(5);
    CHECK(o->use_count() == 1);
    o->retain();
    CHECK(o->use_count() == 2);
    o->retain();
    CHECK(o->use_count() == 3);
    o->release();
    CHECK(o->use_count() == 2);
    o->release();
    CHECK(o->use_count() == 1);
    // The extern "C" ABI is the same mechanism and is null-safe.
    mxs_retain(o);
    CHECK(o->use_count() == 2);
    mxs_release(o);
    CHECK(o->use_count() == 1);
    mxs_release(nullptr);// no crash on null
    o->release();// drops the last reference -> the object is freed here
}

MX_TEST(dynamic_dispatch_ops) {
    // The generic mxs_op_* dispatch over real types (docs §8) — the symbols codegen lowers to.
    auto repr = [](MXObject *o) { return o->repr(); };
    // int + int stays an exact MXInteger
    auto *s = mxs_op_add(MXInteger::from_literal("2").release(),
                         MXInteger::from_literal("3").release());
    CHECK(dynamic_cast<const MXInteger *>(s) != nullptr);
    CHECK(repr(s) == "5");
    // int + float promotes to MXFloat
    auto *p = mxs_op_add(MXInteger::from_literal("2").release(), new MXFloat(1.5));
    CHECK(dynamic_cast<const MXFloat *>(p) != nullptr);
    CHECK(repr(p) == "3.5");
    // string + string concatenates
    auto *c = mxs_op_add(new MXString("foo"), new MXString("bar"));
    CHECK(dynamic_cast<const MXString *>(c) != nullptr);
    CHECK(dynamic_cast<const MXString *>(c)->str() == "foobar");
    CHECK(repr(c) == "\"foobar\"");// repr() quotes strings now (D-STR-REPR)
    // string + number is a type error (an MXError object, not a crash)
    auto *e = mxs_op_add(new MXString("x"), MXInteger::from_literal("1").release());
    CHECK(dynamic_cast<const mxs::core::MXError *>(e) != nullptr);
    // int / int -> MXInteger; float in -> MXFloat
    CHECK(repr(mxs_op_div(MXInteger::from_literal("10").release(),
                          MXInteger::from_literal("4").release())) == "2");
    CHECK(repr(mxs_op_div(new MXFloat(10.0), MXInteger::from_literal("4").release())) ==
          "2.5");
    // cross-type numeric comparison
    CHECK(dynamic_cast<const MXBoolean *>(
                  mxs_op_lt(MXInteger::from_literal("2").release(), new MXFloat(2.5)))
                  ->value());
    // equality across int/float; and eq is total (int vs string -> false, no error)
    CHECK(dynamic_cast<const MXBoolean *>(
                  mxs_op_eq(MXInteger::from_literal("2").release(), new MXFloat(2.0)))
                  ->value());
    CHECK(!dynamic_cast<const MXBoolean *>(
                   mxs_op_eq(MXInteger::from_literal("1").release(), new MXString("1")))
                   ->value());
}

namespace {
    int g_instance_dtor_calls = 0;
    void instance_test_dtor(mxs::core::MXObject *self) {
        (void) self;
        ++g_instance_dtor_calls;
    }
    // A user operator+ that ignores its operands and returns MXInteger(999) — proves the
    // vtable-slot routing in mxs_op_add reaches the user operator.
    mxs::core::MXObject *instance_test_op_add(mxs::core::MXObject *a,
                                              mxs::core::MXObject *b) {
        (void) a;
        (void) b;
        return MXInteger::from_literal("999").release();
    }
}// namespace

MX_TEST(instance_fields_repr_and_type) {
    using mxs::core::MXClassInfo;
    static void *vt[mxs::core::MX_SLOT_RESERVED_COUNT] = { };
    static const MXClassInfo ci{ "TestPt", nullptr, nullptr,
                                 mxs::core::MX_SLOT_RESERVED_COUNT, vt };
    auto *inst = mxs_instance_new(&ci);
    CHECK(inst != nullptr);
    CHECK(mxs_object_classinfo(inst) == &ci);
    CHECK(mxs_is_type(inst, "TestPt") == 1);
    CHECK(mxs_is_type(inst, "Other") == 0);
    CHECK(mxs_is_type(inst, "Object") == 1);// every object is an Object
    mxs_set_attr(inst, "x", MXInteger::from_literal("3").release());
    mxs_set_attr(inst, "y", MXInteger::from_literal("4").release());
    auto *fx = mxs_get_attr(inst, "x");// returns the field, retained (+1)
    CHECK(dynamic_cast<const MXInteger *>(fx)->to_decimal() == "3");
    fx->release();
    CHECK(inst->repr() == "TestPt(x=3, y=4)");
    auto *fz = mxs_get_attr(inst, "z");// unset field -> nil (+1)
    CHECK(dynamic_cast<const MXNil *>(fz) != nullptr);
    fz->release();
    inst->release();// drops the instance, releasing its owned field objects
}

MX_TEST(instance_destructor_and_field_arc) {
    using mxs::core::MXClassInfo;
    using mxs::core::MXPopulationManager;
    static void *vt[mxs::core::MX_SLOT_RESERVED_COUNT] = { };
    static const MXClassInfo ci{ "Res", nullptr, &instance_test_dtor,
                                 mxs::core::MX_SLOT_RESERVED_COUNT, vt };
    g_instance_dtor_calls = 0;
    const std::size_t before = MXPopulationManager::get_manager().population_count();
    auto *inst = mxs_instance_new(&ci);
    mxs_set_attr(inst, "v", MXInteger::from_literal("42").release());
    CHECK(g_instance_dtor_calls == 0);
    inst->release();// rc 0 -> ~MXInstance: runs the user dtor, then releases the field
    CHECK(g_instance_dtor_calls == 1);// the destructor fired exactly once
    // Instance + field are both freed -> the live-object count returns to baseline (no leak).
    CHECK(MXPopulationManager::get_manager().population_count() == before);
}

MX_TEST(instance_operator_overload_dispatch) {
    using mxs::core::MXClassInfo;
    static void *vt[mxs::core::MX_SLOT_RESERVED_COUNT] = { };
    vt[mxs::core::MX_SLOT_OP_ADD] = reinterpret_cast<void *>(&instance_test_op_add);
    static const MXClassInfo ci{ "Adder", nullptr, nullptr,
                                 mxs::core::MX_SLOT_RESERVED_COUNT, vt };
    auto *inst = mxs_instance_new(&ci);
    // mxs_op_add sees `inst`'s class overrides OP_ADD -> dispatches to the user operator.
    auto *one = MXInteger::from_literal("1").release();
    auto *r = mxs_op_add(inst, one);
    CHECK(dynamic_cast<const MXInteger *>(r)->to_decimal() == "999");
    r->release();
    one->release();
    // An instance whose class does NOT override OP_ADD falls back to the builtin path
    // (instance + int is unsupported -> an MXError, not a crash).
    static void *vt2[mxs::core::MX_SLOT_RESERVED_COUNT] = { };
    static const MXClassInfo ci2{ "Plain", nullptr, nullptr,
                                  mxs::core::MX_SLOT_RESERVED_COUNT, vt2 };
    auto *inst2 = mxs_instance_new(&ci2);
    auto *two = MXInteger::from_literal("1").release();
    auto *e = mxs_op_add(inst2, two);
    CHECK(dynamic_cast<const mxs::core::MXError *>(e) != nullptr);
    e->release();
    two->release();
    inst->release();
    inst2->release();
}

// ---- progress12: stdio (str/repr split + format) -------------------------------------------

MX_TEST(string_str_repr_split) {
    // str() = raw bytes (for print); repr() = quoted + escaped (containers / REPL / {:?}).
    CHECK(MXString("hi").str() == "hi");
    CHECK(MXString("hi").repr() == "\"hi\"");
    // the common escapes round-trip in repr()
    CHECK(MXString(std::string("a\"b\n\t\\")).repr() == "\"a\\\"b\\n\\t\\\\\"");
    // a list of strings now shows quoted elements (the container-ambiguity fix)
    MXArrayList xs;
    auto *s1 = new MXString("hi");
    xs.append(s1);
    s1->release();
    auto *s2 = new MXString("yo");
    xs.append(s2);
    s2->release();
    CHECK(xs.repr() == "[\"hi\", \"yo\"]");
    // a scalar's two forms coincide (only strings differ)
    CHECK(MXInteger::from_literal("42")->str() == "42");
    CHECK(MXInteger::from_literal("42")->repr() == "42");
    // mxs_str / mxs_repr builtins: a fresh MXString of the right form; nil -> "nil"
    auto *arg = new MXString("x");
    auto *sr = mxs_str(arg);
    CHECK(dynamic_cast<const MXString *>(sr)->value() == "x");
    auto *rr = mxs_repr(arg);
    CHECK(dynamic_cast<const MXString *>(rr)->value() == "\"x\"");
    sr->release();
    rr->release();
    arg->release();
    auto *ns = mxs_str(nullptr);
    CHECK(dynamic_cast<const MXString *>(ns)->value() == "nil");
    ns->release();
}

MX_TEST(stdio_format) {
    // Pack a list of (already +1) objects, releasing each caller-ref after the list adopts it.
    auto mklist = [](std::initializer_list<MXObject *> items) {
        auto *l = mxs_arraylist_new();
        for (auto *it : items) {
            mxs_arraylist_append(l, it);
            it->release();
        }
        return l;
    };
    auto fmt = [&](const char *f, std::initializer_list<MXObject *> items) {
        auto *fObj = new MXString(f);
        auto *args = mklist(items);
        auto *r = mxs_format(fObj, args);
        const auto *rs = dynamic_cast<const MXString *>(r);
        std::string out = rs ? rs->value() : std::string("<err>");
        r->release();
        fObj->release();
        args->release();
        return out;
    };
    using I = MXInteger;
    CHECK(fmt("{} + {} = {}",
              { I::from_literal("1").release(), I::from_literal("2").release(),
                I::from_literal("3").release() }) == "1 + 2 = 3");
    CHECK(fmt("{0} {1} {0}", { new MXString("a"), new MXString("b") }) == "a b a");
    CHECK(fmt("{{}}", { }) == "{}");
    CHECK(fmt("[{:>5}]", { new MXString("hi") }) == "[   hi]");
    CHECK(fmt("[{:<5}]", { new MXString("hi") }) == "[hi   ]");
    CHECK(fmt("[{:^6}]", { new MXString("hi") }) == "[  hi  ]");
    CHECK(fmt("[{:*^6}]", { new MXString("hi") }) == "[**hi**]");
    CHECK(fmt("{:.2}", { new MXFloat(3.14159) }) == "3.14");
    CHECK(fmt("{:?}", { new MXString("x") }) == "\"x\"");
    // out-of-range index -> an MXError value (no crash)
    auto *fObj = new MXString("{5}");
    auto *args = mklist({ I::from_literal("1").release() });
    auto *r = mxs_format(fObj, args);
    CHECK(dynamic_cast<const mxs::core::MXError *>(r) != nullptr);
    r->release();
    fObj->release();
    args->release();
}

int main() { return mxtest::run_all(); }
