#pragma once
#include "MXMacro.h"
#include "MXType.h"
#include "_type_def.h"
#include <mutex>
#include <string>
#include <unordered_map>

namespace mxs::core {
    using property_name_t = std::string;
    using repr_t = std::string;
    class MXS_API MXObject {
    public:
        const bool is_static;

    public:
        virtual ~MXObject();
        MXObject(bool is_static);
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

        virtual auto equals(MXObjectConstBorrow other) -> bool;
        virtual auto get_hash_code() const -> MXHashCode_t;
        virtual auto register_properties(const property_name_t &name, MXObjectOwned value)
                -> MXObjectOwned;
        virtual auto register_properties(const property_name_t &name,
                                         MXObjectShared value) -> void;
        virtual auto unregister_properties(const property_name_t &name) -> MXObjectOwned;
        virtual auto refer_property(const property_name_t &name) -> MXObjectConstBorrow;
        virtual auto repr() const -> repr_t;

    private:
        std::unordered_map<std::string, MXObjectOwned> dynamic_owned_properties;
        std::unordered_map<std::string, MXObjectShared> dynamic_shared_properties;
        std::mutex lock;
    };

    template<class T = MXObject>
    auto mx_get_mxobject_rtti_instance() -> const MXRuntimeTypeInfo & {
        return T::rtti;
    }
    template<class T = MXObject>
    auto mx_get_mxobject_rtti_ptr() -> const MXRuntimeTypeInfo * {
        return &T::rtti;
    }


}
namespace std {
    template<typename T>
        requires std::is_base_of_v<mxs::core::MXObject, T>
    struct formatter<T> : formatter<std::string> {
        auto format(const T &obj, format_context &ctx) const {
            return formatter<std::string>::format(obj.repr(), ctx);
        }
    };
}
