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

        [[nodiscard]] auto message() const -> const message_t & { return message_; }
        [[nodiscard]] auto error_type() const -> const error_type_name_t & {
            return error_type_;
        }

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