// Unit tests for the core C++ object types (real MXObject subclasses). Links `core`.
// Currently: MXInteger (arbitrary-precision integer, progress09).
#include "test_framework.h"

#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXBoolean.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXString.h"

#include <memory>
#include <string>

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
    // repr is the raw bytes; from_literal builds an MXString
    CHECK(MXString("hi").repr() == "hi");
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
    // keep element objects alive for the duration of the test (list borrows them)
    auto a = MXInteger::from_literal("10");
    auto b = MXInteger::from_literal("20");
    auto c = MXInteger::from_literal("30");
    MXArrayList xs;
    CHECK(dynamic_cast<const MXInteger *>(xs.length().get())->to_decimal() == "0");
    CHECK(xs.repr() == "[]");
    xs.append(a.get());
    xs.append(b.get());
    xs.append(c.get());
    CHECK(xs.size() == 3);
    CHECK(dynamic_cast<const MXInteger *>(xs.length().get())->to_decimal() == "3");
    CHECK(xs.repr() == "[10, 20, 30]");
    CHECK(xs.get(0) == a.get());
    CHECK(dynamic_cast<const MXInteger *>(xs.get(2))->to_decimal() == "30");
    CHECK(xs.get(3) == nullptr);// out of range -> nullptr (C++ API)
    CHECK(xs.get(-1) == nullptr);
    // set
    auto d = MXInteger::from_literal("99");
    CHECK(xs.set(1, d.get()));
    CHECK(xs.repr() == "[10, 99, 30]");
    CHECK(!xs.set(5, d.get()));
    // concat -> a new list
    MXArrayList ys;
    auto e = MXInteger::from_literal("40");
    ys.append(e.get());
    auto z = xs.concat(ys);
    CHECK(dynamic_cast<const MXArrayList *>(z.get())->size() == 4);
    CHECK(dynamic_cast<const MXArrayList *>(z.get())->repr() == "[10, 99, 30, 40]");
    CHECK(xs.size() == 3);// operands unchanged
    // RTTI
    CHECK(std::string(MXArrayList::get_rtti().name) == "MXArrayList");
    CHECK(MXArrayList::get_rtti().parent == &MXObject::get_rtti());
}

int main() { return mxtest::run_all(); }
