#pragma once

#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
namespace mxs::core {
    class MXS_API MXDynamicTypeInfoManager : public MXObject {
    public:
    private:
        mutable std::mutex lock;
        std::unordered_set<const MXRuntimeTypeInfo*> type_infos;
        MXDynamicTypeInfoManager()=default;
        ~MXDynamicTypeInfoManager()=default;
        MXDynamicTypeInfoManager(const MXDynamicTypeInfoManager&)=delete;
    public:
        auto register_newtype(const MXRuntimeTypeInfo * type_ptr) -> void;
        auto unregister_type(const MXRuntimeTypeInfo *const obj) -> void;

        static auto get_manager() -> MXDynamicTypeInfoManager &;
        static auto get_rtti() -> MXRuntimeTypeInfo &;
        auto repr() const -> std::string override;
    };
    };
