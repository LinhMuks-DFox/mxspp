#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

#include <string>
#include <unordered_map>

namespace mxs::builtin {

    // MXInstance — a user-defined class instance (progress09/OOP): it knows its class name and
    // holds named fields. Fields are borrowed MXObject* (element ownership/refcount deferred, as
    // elsewhere). This is the "data class" substrate — fields + constructor + member access;
    // method dispatch is a separate design step. Per-class RTTI / inheritance come later.
    class MXS_API MXInstance : public core::MXObject {
    public:
        explicit MXInstance(std::string class_name, bool is_static = false);

        auto set_field(const std::string &name, core::MXObject *value) -> void;
        // Borrowed field value, or nullptr if the field is unset.
        [[nodiscard]] auto get_field(const std::string &name) const -> core::MXObject *;
        [[nodiscard]] auto class_name() const -> const std::string & { return class_name_; }

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;// "ClassName(f=v, …)"

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        std::string class_name_;
        // Insertion-ordered fields (for a stable repr).
        std::vector<std::pair<std::string, core::MXObject *>> fields_;
    };

}// namespace mxs::builtin
