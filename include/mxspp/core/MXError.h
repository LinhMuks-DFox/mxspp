#pragma once

#include "MXObject.h"
#include "_type_def.h"// Assuming this contains MXObjectOwned, repr_t, etc.
#include <string>

namespace mxs::core {

    using message_t = std::string;
    using error_type_name_t = std::string;

    class MXS_API MXError : public MXObject {
    public:
        explicit MXError(error_type_name_t error_type, message_t message,
                         MXObjectOwned alternative = nullptr, bool panic = false,
                         bool is_static = false);

        ~MXError() override;
        // --- Overrides ---
        [[nodiscard]] auto repr() const -> repr_t override;

        // --- RTTI ---
        static auto get_rtti() -> const MXRuntimeTypeInfo &;

    private:
        error_type_name_t error_type_;
        message_t message_;
        MXObjectOwned alternative_;
        bool panic_;
    };
}