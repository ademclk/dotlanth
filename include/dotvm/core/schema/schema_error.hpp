#pragma once

/// @file schema_error.hpp
/// @brief DEP-003 Schema-specific error codes
///
/// Error codes for Object Type Schema operations, covering type management,
/// property validation, link constraints, and migration operations.

#include <cstdint>
#include <format>
#include <string_view>

namespace dotvm::core::schema {

// ============================================================================
// SchemaError Enum
// ============================================================================

/// @brief Error codes for schema operations
///
/// Error codes are grouped by category in the 176-191 range:
/// - 176-179: Type management errors
/// - 180-183: Property errors
/// - 184-187: Validation errors
/// - 188-189: Link errors
/// - 190-191: Migration errors
enum class SchemaError : std::uint8_t {
    // Type management errors (176-179)
    TypeNotFound = 176,       ///< Object type does not exist in registry
    TypeAlreadyExists = 177,  ///< Object type name already registered
    InvalidTypeName = 178,    ///< Type name is empty or invalid
    MaxTypesExceeded = 179,   ///< Maximum number of types in registry exceeded

    // Property errors (180-183)
    PropertyNotFound = 180,         ///< Property does not exist on type
    PropertyAlreadyExists = 181,    ///< Property name already exists on type
    InvalidPropertyType = 182,      ///< Property type mismatch or invalid
    RequiredPropertyMissing = 183,  ///< Required property has nil/missing value

    // Validation errors (184-187)
    ValidationFailed = 184,       ///< Generic validation failure
    RangeValidationFailed = 185,  ///< Value outside allowed range
    RegexValidationFailed = 186,  ///< Value does not match regex pattern
    EnumValidationFailed = 187,   ///< Value not in allowed enum set

    // Link errors (188-189)
    LinkNotFound = 188,          ///< Link does not exist on type
    CardinalityViolation = 189,  ///< Link cardinality constraint violated

    // Migration errors (190-191)
    MigrationFailed = 190,  ///< Migration operation failed
    VersionConflict = 191,  ///< Schema version conflict during migration
};

/// @brief Convert SchemaError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(SchemaError error) noexcept {
    switch (error) {
        case SchemaError::TypeNotFound:
            return "TypeNotFound";
        case SchemaError::TypeAlreadyExists:
            return "TypeAlreadyExists";
        case SchemaError::InvalidTypeName:
            return "InvalidTypeName";
        case SchemaError::MaxTypesExceeded:
            return "MaxTypesExceeded";
        case SchemaError::PropertyNotFound:
            return "PropertyNotFound";
        case SchemaError::PropertyAlreadyExists:
            return "PropertyAlreadyExists";
        case SchemaError::InvalidPropertyType:
            return "InvalidPropertyType";
        case SchemaError::RequiredPropertyMissing:
            return "RequiredPropertyMissing";
        case SchemaError::ValidationFailed:
            return "ValidationFailed";
        case SchemaError::RangeValidationFailed:
            return "RangeValidationFailed";
        case SchemaError::RegexValidationFailed:
            return "RegexValidationFailed";
        case SchemaError::EnumValidationFailed:
            return "EnumValidationFailed";
        case SchemaError::LinkNotFound:
            return "LinkNotFound";
        case SchemaError::CardinalityViolation:
            return "CardinalityViolation";
        case SchemaError::MigrationFailed:
            return "MigrationFailed";
        case SchemaError::VersionConflict:
            return "VersionConflict";
    }
    return "Unknown";
}

/// @brief Check if a schema error is related to validation
///
/// @param error The error to check
/// @return true if the error is a validation-related error
[[nodiscard]] constexpr bool is_validation_error(SchemaError error) noexcept {
    const auto code = static_cast<std::uint8_t>(error);
    return code >= 184 && code <= 187;
}

/// @brief Check if a schema error is related to type management
///
/// @param error The error to check
/// @return true if the error is a type-related error
[[nodiscard]] constexpr bool is_type_error(SchemaError error) noexcept {
    const auto code = static_cast<std::uint8_t>(error);
    return code >= 176 && code <= 179;
}

/// @brief Check if a schema error is related to properties
///
/// @param error The error to check
/// @return true if the error is a property-related error
[[nodiscard]] constexpr bool is_property_error(SchemaError error) noexcept {
    const auto code = static_cast<std::uint8_t>(error);
    return code >= 180 && code <= 183;
}

}  // namespace dotvm::core::schema

// ============================================================================
// std::formatter specialization for SchemaError
// ============================================================================

template <>
struct std::formatter<dotvm::core::schema::SchemaError> : std::formatter<std::string_view> {
    auto format(dotvm::core::schema::SchemaError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
