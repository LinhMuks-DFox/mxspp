#pragma once

#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXType.h"
namespace mxs::core {
    class MXS_API MXDynamicTypeInfoManager : public MXObject {
    public:
    private:
        mutable std::mutex lock;
        std::unordered_set<const MXRuntimeTypeInfo *> type_infos;

    public:
        MXDynamicTypeInfoManager(const MXDynamicTypeInfoManager &) = delete;
        MXDynamicTypeInfoManager() = delete;
        MXDynamicTypeInfoManager(MXRuntimeTypeInfo &&) = delete;
        auto register_newtype(const MXRuntimeTypeInfo *type_ptr) -> void;
        auto unregister_type(const MXRuntimeTypeInfo *const obj) -> void;

        static auto get_manager() -> MXDynamicTypeInfoManager &;
        static auto get_rtti() -> MXRuntimeTypeInfo &;
        auto repr() const -> std::string override;
    };
};
