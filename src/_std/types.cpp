#include "mxspp/_std/types.h"

#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXBoolean.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXFloat.h"
#include "mxspp/core/MXInstance.h"
#include "mxspp/core/MXInteger.h"
#include "mxspp/core/MXNil.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXString.h"

// ============================================================================================
// std.types — reflection (progress17: relocated from src/core/MXOps.cpp). `typeof` maps a value to
// its mxs type name (the inverse of the runtime mxs_is_type mapping).
// ============================================================================================
namespace {
    using mxs::builtin::MXBoolean;
    using mxs::builtin::MXFloat;
    using mxs::builtin::MXInstance;
    using mxs::builtin::MXInteger;
    using mxs::builtin::MXString;
    using mxs::core::MXError;
    using mxs::core::MXObject;

    template<class T>
    const T *as(const MXObject *o) {
        return dynamic_cast<const T *>(o);
    }
}// namespace

extern "C" {

// The mxs type name of a value, as a fresh MXString (the inverse of mxs_is_type's mapping): a user
// instance -> its class name (MXClassInfo->name); built-ins -> the canonical name
// (int/float/str/bool/nil/List/Error). Backs `std.types.typeof`. Foreign-borrow ABI: returns a +1
// the caller owns.
MXObject *mxs_typeof(const MXObject *o) {
    if (!o) return new MXString("nil");
    if (const auto *inst = as<MXInstance>(o)) return new MXString(inst->class_name());
    if (as<MXError>(o)) return new MXString("Error");
    if (as<MXInteger>(o)) return new MXString("int");
    if (as<MXFloat>(o)) return new MXString("float");
    if (as<MXString>(o)) return new MXString("str");
    if (as<MXBoolean>(o)) return new MXString("bool");
    if (as<mxs::builtin::MXNil>(o)) return new MXString("nil");
    if (as<mxs::builtin::MXArrayList>(o)) return new MXString("List");
    return new MXString(o->get_rtti().name);
}

}// extern "C"
