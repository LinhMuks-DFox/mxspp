#pragma once
#include <cstdint>

// MXClassInfo — the per-class type descriptor (progress11 / progress09 vtable design). It is the
// STABLE LAYOUT CONTRACT shared by two worlds that must agree byte-for-byte:
//   * the C++ runtime compiled into core.bc (reads name / destructor / vtable[slot]); and
//   * the backend codegen, which emits each class's MXClassInfo + vtable as LLVM *constant globals*
//     (`{ ptr, ptr, ptr, i64, ptr }`) so LLVM can fold a dispatch to a direct call + cross-boundary
//     inline whenever the receiver's runtime type is statically known (the D6 win).
// This header is intentionally LLVM-free so the core.bc sources lower to plain bitcode.

namespace mxs::core {

    class MXObject;// the runtime object base (defined in MXObject.h)

    struct MXClassInfo {
        const char *name;// class name (RTTI / is_type / repr)
        const MXClassInfo *parent;// base class; nullptr in v1 (inheritance-ready)
        void (*destructor)(MXObject *self);// user ~Class() trampoline, or nullptr if none
        std::int64_t vtable_len;// = MX_SLOT_RESERVED_COUNT + #global method selectors
        void *const *vtable;// -> constant array of fn ptrs (opaque; caller casts)
    };

    // Reserved low vtable slots, shared by codegen and core.bc: the builtin operators and the
    // `Object` virtuals. A slot is **null** when the class does not override it; operator routing
    // (mxs_op_*) checks `!= nullptr` before dispatching to a user operator. User method selectors
    // are assigned whole-program slots starting at MX_SLOT_RESERVED_COUNT.
    enum MXSlot : std::int64_t {
        MX_SLOT_OP_ADD = 0,
        MX_SLOT_OP_SUB,
        MX_SLOT_OP_MUL,
        MX_SLOT_OP_DIV,
        MX_SLOT_OP_MOD,
        MX_SLOT_OP_POW,
        MX_SLOT_OP_LT,
        MX_SLOT_OP_LE,
        MX_SLOT_OP_GT,
        MX_SLOT_OP_GE,
        MX_SLOT_OP_EQ,
        MX_SLOT_OP_NE,
        MX_SLOT_OP_NEG,
        MX_SLOT_OP_NOT,
        MX_SLOT_OP_INDEX_GET,
        MX_SLOT_OP_INDEX_SET,
        MX_SLOT_REPR,
        MX_SLOT_HASH,
        MX_SLOT_EQUALS,
        MX_SLOT_RESERVED_COUNT// first user-method selector slot
    };

}// namespace mxs::core
