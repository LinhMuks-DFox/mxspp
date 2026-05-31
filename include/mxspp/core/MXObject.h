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

        // Reference counting (the runtime ownership model, progress09 D-temporaries). A freshly
        // constructed object starts with a count of 1 (the creator's reference). `retain` adds a
        // reference; `release` drops one and deletes the object when the count reaches zero.
        // The JIT-facing `mxs_retain` / `mxs_release` ABI wraps these. (Single-threaded for now;
        // an atomic count comes with concurrency.)
        auto retain() const -> void;
        auto release() const -> void;
        [[nodiscard]] auto use_count() const -> std::size_t;

    private:
        std::unordered_map<std::string, MXObjectOwned> dynamic_owned_properties;
        std::unordered_map<std::string, MXObjectShared> dynamic_shared_properties;
        std::mutex lock;
        mutable std::size_t refcount_ = 1;
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
