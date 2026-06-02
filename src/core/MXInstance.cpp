#include "mxspp/core/MXInstance.h"
#include "mxspp/core/MXClassInfo.h"
#include "mxspp/core/MXObject.h"

#include <string>

namespace mxs::builtin {

    MXInstance::MXInstance(const core::MXClassInfo *classinfo, bool is_static)
        : MXObject(is_static), classinfo_(classinfo) { }

    MXInstance::~MXInstance() {
        // Run the user destructor (~Class()) first, while the fields are still alive, then release
        // the owned field references (the ARC protocol: an instance owns its fields).
        if (classinfo_ && classinfo_->destructor) classinfo_->destructor(this);
        for (auto &[name, value] : fields_)
            if (value) value->release();
    }

    auto MXInstance::set_field(const std::string &name, core::MXObject *value) -> void {
        // Adopt `value` (takes the +1 the caller produced). If the field already exists, release
        // the previous value first.
        for (auto &[fname, fvalue] : fields_) {
            if (fname == name) {
                if (fvalue == value) return;// self-assign: keep the single reference
                if (fvalue) fvalue->release();
                fvalue = value;
                return;
            }
        }
        fields_.emplace_back(name, value);
    }

    auto MXInstance::get_field(const std::string &name) const -> core::MXObject * {
        for (const auto &[fname, fvalue] : fields_)
            if (fname == name) return fvalue;
        return nullptr;
    }

    auto MXInstance::repr() const -> core::repr_t {
        std::string out = class_name();
        out += "(";
        bool first = true;
        for (const auto &[fname, fvalue] : fields_) {
            if (!first) out += ", ";
            first = false;
            out += fname;
            out += "=";
            out += fvalue ? fvalue->repr() : "nil";
        }
        out += ")";
        return out;
    }

    auto MXInstance::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXInstance",
                                                 &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin
