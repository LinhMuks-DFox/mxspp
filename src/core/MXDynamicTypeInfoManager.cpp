//
// Created by mux on 2025/7/10.
//
#include "mxspp/core/MXDynamicTypeTable.h"

namespace mxs::core {
     auto MXDynamicTypeInfoManager::get_manager() -> MXDynamicTypeInfoManager & {
         static MXDynamicTypeInfoManager manager;
         return manager;
     }

    auto MXDynamicTypeInfoManager::get_rtti() -> MXRuntimeTypeInfo & {
         static MXRuntimeTypeInfo rtti {
             "mxs::core::MXDynamicTypeInfoManager", &MXObject::get_rtti(),
         };

         return rtti;
     }
    auto MXDynamicTypeInfoManager::register_newtype(const MXRuntimeTypeInfo *const obj) -> void {
         std::scoped_lock guard(this->lock);
         this->type_infos.insert(obj);
     }

    auto MXDynamicTypeInfoManager::unregister_type(const MXRuntimeTypeInfo *const obj) -> void {
         std::scoped_lock guard(this->lock);
         this->type_infos.erase(obj);
     }

    auto MXDynamicTypeInfoManager::repr() const -> std::string {
         return "MXDynamicTypeInfoManager";
     }





}