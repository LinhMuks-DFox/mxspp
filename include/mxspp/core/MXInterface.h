#pragma once
#include "MXMacro.h"
#include "MXObject.h"
#include "_type_def.h"
#include <llvm/IR/Module.h>
namespace mxs::core {
    class MXS_API MXInterface : public virtual MXObject { };

    class MXS_API MXIClone : public MXInterface {
    public:
        virtual auto clone() const -> MXObjectOwned = 0;
    };

    class MXS_API MXIEmitLLVM : public MXInterface {
    public:
        virtual auto emit_llvm(llvm::Module &Module) const -> void = 0;
    };
}