#pragma once
#include "mxspp/core/MXMacro.h"
#include "mxspp/core/MXObject.h"
#include "mxspp/core/_type_def.h"

#include <cstdint>
#include <vector>

namespace mxs::builtin {

    // MXArrayList — MXScript's dynamic array (docs §3.3 "List<T>", named ArrayList per
    // progress07 D1): a real core::MXObject holding an ordered sequence of element objects.
    //
    // Elements are stored as borrowed core::MXObject* (the doc's "vector of MXObject*"). Element
    // *ownership* (reference counting / an arena) is deferred to the runtime ownership layer
    // (progress07 D3, progress09 open Qs) — for now the list does not own/free its elements,
    // matching the rest of the current runtime. Two-layer API (C++ + extern "C"), per progress09.
    class MXS_API MXArrayList : public core::MXObject {
    public:
        explicit MXArrayList(bool is_static = false);

        auto append(core::MXObject *item) -> void;
        // Borrowed element at `index`, or nullptr if out of range (the extern "C" layer maps
        // out-of-range to an MXError, forward-compatible with the match-based error model).
        [[nodiscard]] auto get(std::int64_t index) const -> core::MXObject *;
        auto set(std::int64_t index, core::MXObject *item)
                -> bool;// false if out of range
        [[nodiscard]] auto size() const -> std::size_t { return items_.size(); }
        [[nodiscard]] auto length() const -> MXObjectOwned;// size as an MXInteger
        // A new MXArrayList with this list's elements followed by other's (shares element ptrs).
        [[nodiscard]] auto concat(const MXArrayList &other) const -> MXObjectOwned;

        // --- MXObject overrides ---
        [[nodiscard]] auto repr() const -> core::repr_t override;// "[a, b, c]"
        [[nodiscard]] auto is_truthy() const -> bool override { return !items_.empty(); }
        [[nodiscard]] auto get_hash_code() const -> MXHashCode_t override;

        // --- RTTI ---
        static auto get_rtti() -> const core::MXRuntimeTypeInfo &;

    private:
        std::vector<core::MXObject *> items_;
    };

}// namespace mxs::builtin
