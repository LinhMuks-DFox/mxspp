#include "mxspp/core/MXArrayList.h"
#include "mxspp/core/MXError.h"
#include "mxspp/core/MXInteger.h"

#include <cstdint>
#include <string>

namespace mxs::builtin {

    MXArrayList::MXArrayList(bool is_static) : core::MXObject(is_static) { }

    auto MXArrayList::append(core::MXObject *item) -> void { items_.push_back(item); }

    auto MXArrayList::get(std::int64_t index) const -> core::MXObject * {
        if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) return nullptr;
        return items_[static_cast<std::size_t>(index)];
    }

    auto MXArrayList::set(std::int64_t index, core::MXObject *item) -> bool {
        if (index < 0 || static_cast<std::size_t>(index) >= items_.size()) return false;
        items_[static_cast<std::size_t>(index)] = item;
        return true;
    }

    auto MXArrayList::length() const -> MXObjectOwned {
        return std::make_unique<MXInteger>(static_cast<std::int64_t>(items_.size()));
    }

    auto MXArrayList::concat(const MXArrayList &other) const -> MXObjectOwned {
        auto out = std::make_unique<MXArrayList>();
        out->items_ = items_;
        out->items_.insert(out->items_.end(), other.items_.begin(), other.items_.end());
        return out;
    }

    auto MXArrayList::repr() const -> core::repr_t {
        std::string s = "[";
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (i) s += ", ";
            s += items_[i] ? items_[i]->repr() : "nil";
        }
        s += "]";
        return s;
    }

    auto MXArrayList::get_hash_code() const -> MXHashCode_t {
        // Identity hash for now; structural hashing waits on element ownership/equality.
        return reinterpret_cast<MXHashCode_t>(this);
    }

    auto MXArrayList::get_rtti() -> const core::MXRuntimeTypeInfo & {
        static core::MXRuntimeTypeInfo instance{ "MXArrayList",
                                                 &core::MXObject::get_rtti() };
        return instance;
    }

}// namespace mxs::builtin

// ============================================================================================
// extern "C" ABI — JIT-facing, ABI-stable surface (progress09 D3). Indices are MXInteger
// objects; out-of-range access returns an MXError (the match-based error model handles it).
// ============================================================================================
namespace {
    using mxs::builtin::MXArrayList;
    using mxs::builtin::MXInteger;
    using mxs::core::MXObject;

    MXArrayList *as_list(MXObject *o) { return dynamic_cast<MXArrayList *>(o); }
    bool index_of(MXObject *idx, std::int64_t &out) {
        const auto *i = dynamic_cast<const MXInteger *>(idx);
        if (!i) return false;
        bool ok = false;
        out = i->to_i64(ok);
        return ok;
    }
}// namespace

extern "C" {

MXObject *mxs_arraylist_new() { return new MXArrayList(); }

void mxs_arraylist_append(MXObject *list, MXObject *item) {
    if (auto *l = as_list(list)) l->append(item);
}

MXObject *mxs_arraylist_len(MXObject *list) {
    auto *l = as_list(list);
    return l ? l->length().release() : nullptr;
}

MXObject *mxs_arraylist_get(MXObject *list, MXObject *idx) {
    auto *l = as_list(list);
    std::int64_t i = 0;
    if (!l || !index_of(idx, i))
        return new mxs::core::MXError("TypeError",
                                      "arraylist.get expects a list and an int");
    MXObject *item = l->get(i);
    if (!item)
        return new mxs::core::MXError("IndexError", "arraylist index out of range");
    return item;
}

MXObject *mxs_arraylist_set(MXObject *list, MXObject *idx, MXObject *item) {
    auto *l = as_list(list);
    std::int64_t i = 0;
    if (!l || !index_of(idx, i))
        return new mxs::core::MXError("TypeError",
                                      "arraylist.set expects a list and an int");
    if (!l->set(i, item))
        return new mxs::core::MXError("IndexError", "arraylist index out of range");
    return list;
}

MXObject *mxs_arraylist_concat(MXObject *a, MXObject *b) {
    auto *la = as_list(a), *lb = as_list(b);
    if (!la || !lb) return new mxs::core::MXError("TypeError", "expected two arraylists");
    return la->concat(*lb).release();
}

}// extern "C"
