#pragma once
#include "mxspp/core/MXClassInfo.h"
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

#include <string>
#include <utility>
#include <vector>

namespace mxs::builtin {

    // MXInstance — a user-defined class instance (progress11 OOP v1). It carries a pointer to its
    // class's MXClassInfo (name + destructor + vtable; the type descriptor codegen emits as a
    // constant global) and holds named, insertion-ordered fields.
    //
    // Ownership (the ARC protocol, progress11): the instance OWNS strong references to its fields.
    // `set_field` adopts the value's +1 (no extra retain) and releases the previous value when a
    // field is overwritten; the destructor runs the user `~Class()` trampoline (if any) and then
    // releases every field. (Single inheritance is deferred — `classinfo_->parent` is nullptr in v1.)
    class MXS_API MXInstance : public core::MXObject {
    public:
        explicit MXInstance(const core::MXClassInfo *classinfo, bool is_static = false);
        ~MXInstance() override;

        // Adopts `value` (takes its +1 reference); releases the prior value if `name` was set.
        auto set_field(const std::string &name, core::MXObject *value) -> void;
        // Borrowed field value, or nullptr if the field is unset.
        [[nodiscard]] auto get_field(const std::string &name) const -> core::MXObject *;

        [[nodiscard]] auto classinfo() const -> const core::MXClassInfo * {
            return classinfo_;
        }
        [[nodiscard]] auto class_name() const -> const char * {
            return classinfo_ ? classinfo_->name : "<anon>";
        }

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;// "ClassName(f=v, …)"

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        const core::MXClassInfo *classinfo_;
        // Insertion-ordered, OWNED fields (for a stable repr + deterministic destruction order).
        std::vector<std::pair<std::string, core::MXObject *>> fields_;
    };

}// namespace mxs::builtin
