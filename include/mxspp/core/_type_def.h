#pragma once
#include <algorithm>
#include <cstdint>
#include <format>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace mxs {
    namespace core {
        class MXRuntimeTypeInfo;
        class MXObject;
        class MXError;
    }
    using MXTypeInfoName_t = const std::string;
    using MXTypeInfoNamePointer_t = const core::MXRuntimeTypeInfo *;
    using MXObjectOwned = std::unique_ptr<core::MXObject>;
    using MXObjectBorrow = std::unique_ptr<core::MXObject> &;
    using MXObjectConstBorrow = const std::unique_ptr<core::MXObject> &;
    using MXObjectShared = std::shared_ptr<core::MXObject>;
    using MXHashCode_t = std::uint64_t;

}
