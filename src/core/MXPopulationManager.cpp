#include "mxspp/core/MXPopulationManager.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXType.h"
#include <cstddef>
#include <mutex>

namespace mxs::core {
    auto MXPopulationManager::get_manager() -> MXPopulationManager & {
        static MXPopulationManager instance{};
        return instance;
    }
    auto MXPopulationManager::get_rtti() -> MXRuntimeTypeInfo & {
        static MXRuntimeTypeInfo instance{ "mxs::core::MXPopulationManager", nullptr };
        return instance;
    }
    auto MXPopulationManager::register_object(const MXObject *const obj) -> void {
        std::scoped_lock guard(this->lock);
        if (obj) this->populations.insert(obj);
    }
    auto MXPopulationManager::unregister_object(const MXObject *const obj) -> void {
        std::scoped_lock guard(this->lock);
        if (obj) this->populations.erase(obj);
    }

    MXPopulationManager::MXPopulationManager() = default;
    MXPopulationManager::~MXPopulationManager() = default;
    auto MXPopulationManager::repr() const -> std::string {
        std::scoped_lock guard(this->lock);
        std::string result = "MXPopulationManager{";
        bool first = true;

        for (const auto *obj : populations) {
            if (!first) result += ", ";
            first = false;
            if (obj) {
                result += std::format("\n    MXObject at: {}, with repr: {{ \n"
                                      "        {}\n"
                                      "    }}",
                                      static_cast<const void *>(obj), obj->repr());
            } else {
                result += "<nullptr>";
            }
        }

        result += "\n}";
        return result;
    }
}