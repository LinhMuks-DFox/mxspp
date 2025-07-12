#pragma once

#include "MXInterface.h"
#include "MXMacro.h"
#include "MXType.h"
#include "_type_def.h"
#include "mxspp/core/MXObject.h"

#include <vector>
namespace mxs::core {
    using function_name = std::string;

    class MXS_API MXFunction : public virtual MXObject,
                               public virtual MXIEmitLLVM,
                               public virtual MXIClone {
    public:
        const function_name function_name;
        std::vector<MXObjectOwned> argv;
        const MXRuntimeTypeInfo *return_type;

        auto operator()() const -> MXObjectOwned;
        auto call() const -> MXObjectOwned;

        auto append_arg(MXObjectOwned) -> void;


    public: /* Interface */
        auto emit_llvm(llvm::Module &Module) const -> void override;
        auto clone() const -> MXObjectOwned override;
    };
}