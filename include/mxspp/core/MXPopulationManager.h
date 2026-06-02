#pragma once

#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/MXType.h"
#include <mutex>
#include <unordered_set>
namespace mxs::core {
    class MXS_API MXPopulationManager {
    private:
        mutable std::mutex lock;
        std::unordered_set<const MXObject *> populations;
        MXPopulationManager();
        ~MXPopulationManager();

    public:
        auto register_object(const MXObject *const obj) -> void;
        auto unregister_object(const MXObject *const obj) -> void;
        // Number of live MXObjects currently tracked. Used by tests to assert that a scope leaves
        // no leaked / double-freed objects (the ARC verification lever, progress11).
        [[nodiscard]] auto population_count() const -> std::size_t;

        static auto get_manager() -> MXPopulationManager &;
        static auto get_rtti() -> MXRuntimeTypeInfo &;
        auto repr() const -> std::string;
    };
}

namespace std {
    template<>
    struct formatter<mxs::core::MXPopulationManager> : formatter<std::string> {
        auto format(const mxs::core::MXPopulationManager &mgr,
                    format_context &ctx) const {
            return formatter<std::string>::format(mgr.repr(), ctx);
        }
    };
}