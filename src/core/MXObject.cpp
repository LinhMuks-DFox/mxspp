#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXPopulationManager.h"
#include "mxspp/core/MXType.h"
#include "mxspp/core/_type_def.h"
#include "llvm/IR/Instruction.h"
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
}