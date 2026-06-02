#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXPopulationManager.h"
#include "mxspp/core/MXType.h"
#include "mxspp/core/_type_def.h"

#include <cstdio>
namespace mxs::core {

    MXObject::MXObject(bool is_static) : is_static(is_static) {
        MXPopulationManager::get_manager().register_object(this);
    }

    MXObject::~MXObject() { MXPopulationManager::get_manager().unregister_object(this); }

    auto MXObject::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static MXRuntimeTypeInfo instance{ "MXObject", nullptr };
        return instance;
    }

    auto MXObject::get_hash_code() const -> MXHashCode_t {
        return reinterpret_cast<MXHashCode_t>(this);
    }
    auto MXObject::register_properties(const property_name_t &name, MXObjectOwned value)
            -> MXObjectOwned {
        std::scoped_lock guard(this->lock);
        this->dynamic_owned_properties[name] = std::move(value);
        return value;
    }

    auto MXObject::register_properties(const property_name_t &name, MXObjectShared value)
            -> void {
        std::scoped_lock guard(this->lock);
        this->dynamic_shared_properties[name] = value;
    }

    auto MXObject::refer_property(const property_name_t &name) -> MXObjectConstBorrow {
        std::scoped_lock guard(this->lock);
        return this->dynamic_owned_properties.at(name);
    }
    auto MXObject::unregister_properties(const property_name_t &name) -> MXObjectOwned {
        std::scoped_lock guard(this->lock);

        auto it = this->dynamic_owned_properties.find(name);
        if (it == this->dynamic_owned_properties.end()) { return nullptr; }

        // move 出 unique_ptr，释放 map
        MXObjectOwned removed = std::move(it->second);
        this->dynamic_owned_properties.erase(it);
        return removed;
    }

    auto MXObject::equals(MXObjectConstBorrow other) -> bool {
        return other.get() == this;
    }

    auto MXObject::repr() const -> repr_t { return this->get_rtti().name; }
    auto MXObject::str() const -> repr_t { return this->repr(); }
    auto MXObject::is_truthy() const -> bool { return true; }

    auto MXObject::retain() const -> void { ++refcount_; }
    auto MXObject::release() const -> void {
        // Once destruction has begun, ignore further releases: the destructor body (and the
        // teardown of `self`'s binding cell) performs retain/release on `this`, which would
        // otherwise hit zero again and re-enter `delete`, recursing infinitely.
        if (destroying_) return;
        if (--refcount_ == 0) {
            destroying_ = true;
            delete this;
        }
    }
    auto MXObject::use_count() const -> std::size_t { return refcount_; }
}

extern "C" {
// JIT-facing reference-counting ABI (progress09). Null-safe.
void mxs_retain(const mxs::core::MXObject *o) {
    if (o) o->retain();
}
void mxs_release(const mxs::core::MXObject *o) {
    if (o) o->release();
}

// Polymorphic boolean coercion (conditions / logical ops); null is falsey.
std::int64_t mxs_object_truthy(const mxs::core::MXObject *o) {
    return (o && o->is_truthy()) ? 1 : 0;
}

// Polymorphic single-object print via the human form str() (progress12 D-STR-REPR). The variadic
// print/println the language binds live in MXFormat.cpp; these stay for direct single-value use.
void mxs_print_object(const mxs::core::MXObject *o) {
    std::fprintf(stdout, "%s", o ? o->str().c_str() : "nil");
}
void mxs_println_object(const mxs::core::MXObject *o) {
    std::fprintf(stdout, "%s\n", o ? o->str().c_str() : "nil");
}
}