#include "mxspp/core/MXError.h"
#include <format>
#include <utility> // For std::move

namespace mxs::core {
    MXError::MXError(error_type_name_t  error_type,
                     message_t  message,
                     MXObjectOwned alternative,
                     bool panic,
                     bool is_static)
        : MXObject(is_static),
          error_type_(std::move(error_type)),          // Copy from const reference
          message_(std::move(message)),                // Copy from const reference
          alternative_(std::move(alternative)), // Move the unique_ptr to take ownership
          panic_(panic) {}

    MXError::~MXError() = default;

    auto MXError::get_rtti() -> const MXRuntimeTypeInfo& {
        static MXRuntimeTypeInfo instance{"MXError", &MXObject::get_rtti()};
        return instance;
    }

    auto MXError::repr() const -> repr_t {
        return std::format("{}(panic={}): {}", this->get_error_type(),
                           this->is_panic(), this->get_message());
    }
}