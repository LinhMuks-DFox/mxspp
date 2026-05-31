#include "mxspp/core/MXNil.h"

namespace mxs::builtin {

    MXNil::MXNil(bool is_static) : core::MXObject(is_static) { }

    auto MXNil::repr() const -> core::repr_t { return "nil"; }

    auto MXNil::equals(MXObjectConstBorrow other) -> bool {
        return dynamic_cast<const MXNil *>(other.get()) != nullptr;// all nils are equal
    }

    auto MXNil::get_hash_code() const -> MXHashCode_t { return 0; }

    auto MXNil::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXNil", &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin

// ============================================================================================
// extern "C" ABI — JIT-facing, ABI-stable surface (progress09 D3).
// ============================================================================================
extern "C" {
mxs::core::MXObject *mxs_nil_new() { return new mxs::builtin::MXNil(); }
}// extern "C"
