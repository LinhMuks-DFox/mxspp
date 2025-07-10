#pragma once
#include "MXMacro.h"
#include "mxspp/core/_type_def.h"
namespace mxs::core {
    struct MXS_API MXRuntimeTypeInfo {
        const MXTypeInfoName_t name;
        const MXRuntimeTypeInfo *parent;
    };
}