#pragma once

/// @file property_type.hpp
/// @brief DEP-003 Property type definitions for Object Type Schema
///
/// Defines property types that map to the VM's NaN-boxed Value system,
/// along with PropertyDef for defining object type properties.

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dotvm/core/value.hpp"

namespace dotvm::core::schema {

// ============================================================================
// PropertyType Enum
// ============================================================================

/// @brief Property types for object type schema definitions
///
/// These types map to the underlying VM Value system:
/// - Int64: Stored as Value::Integer (48-bit signed)
/// - Float64: Stored as Value::Float (IEEE 754 double)
/// - Boolean: Stored as Value::Bool
/// - String: Stored as Value::Handle (reference to string storage)
/// - DateTime: Stored as Value::Integer (Unix epoch nanoseconds)
/// - Handle: Stored as Value::Handle (reference to another object)
enum class PropertyType : std::uint8_t {
    Int64 = 0,     ///< 64-bit signed integer (stored as 48-bit)
    Float64 = 1,   ///< IEEE 754 double-precision float
    Boolean = 2,   ///< Boolean true/false
    String = 3,    ///< String value (via Handle to string storage)
    DateTime = 4,  ///< Date/time as Unix epoch nanoseconds (Int64)
    Handle = 5,    ///< Handle to another object
};

/// @brief Convert PropertyType to human-readable string
[[nodiscard]] constexpr std::string_view to_string(PropertyType type) noexcept {
    switch (type) {
        case PropertyType::Int64:
            return "Int64";
        case PropertyType::Float64:
            return "Float64";
        case PropertyType::Boolean:
            return "Boolean";
        case PropertyType::String:
            return "String";
        case PropertyType::DateTime:
            return "DateTime";
        case PropertyType::Handle:
            return "Handle";
    }
    return "Unknown";
}

/// @brief Get the underlying ValueType for a PropertyType
///
/// Maps schema property types to the VM's NaN-boxed value types.
/// String and Handle both map to ValueType::Handle.
/// DateTime maps to ValueType::Integer.
///
/// @param type The property type to map
/// @return The corresponding ValueType
[[nodiscard]] constexpr ValueType to_value_type(PropertyType type) noexcept {
    switch (type) {
        case PropertyType::Int64:
            return ValueType::Integer;
        case PropertyType::Float64:
            return ValueType::Float;
        case PropertyType::Boolean:
            return ValueType::Bool;
        case PropertyType::String:
            return ValueType::Handle;  // String stored via handle
        case PropertyType::DateTime:
            return ValueType::Integer;  // Epoch nanoseconds
        case PropertyType::Handle:
            return ValueType::Handle;
    }
    return ValueType::Nil;
}

/// @brief Check if a Value's type is compatible with a PropertyType
///
/// @param value The value to check
/// @param expected The expected property type
/// @return true if the value type matches the property type
[[nodiscard]] constexpr bool is_compatible(Value value, PropertyType expected) noexcept {
    if (value.is_nil()) {
        return true;  // Nil is always compatible (represents missing/null)
    }

    switch (expected) {
        case PropertyType::Int64:
        case PropertyType::DateTime:
            return value.is_integer();
        case PropertyType::Float64:
            return value.is_float();
        case PropertyType::Boolean:
            return value.is_bool();
        case PropertyType::String:
        case PropertyType::Handle:
            return value.is_handle();
    }
    return false;
}

// ============================================================================
// PropertyDef Struct
// ============================================================================

// Forward declaration of Validator (defined in validator.hpp)
// We use a vector here because validators are stored by value

/// @brief Definition of a property on an object type
///
/// Defines the name, type, constraints, and default value for a property.
/// Properties can have multiple validators and an optional default value.
struct PropertyDef {
    /// @brief Property name (must be non-empty and unique within type)
    std::string name;

    /// @brief Property type (maps to VM Value types)
    PropertyType type{PropertyType::String};

    /// @brief Whether this property is required (cannot be nil)
    bool required{false};

    /// @brief Optional default value (must match property type)
    std::optional<Value> default_value{std::nullopt};

    /// @brief For Handle types, the target type name (empty = any type)
    std::string target_type{};

    /// @brief Equality comparison
    [[nodiscard]] bool operator==(const PropertyDef& other) const noexcept {
        return name == other.name && type == other.type && required == other.required &&
               default_value == other.default_value && target_type == other.target_type;
    }
};

// ============================================================================
// PropertyDefBuilder
// ============================================================================

/// @brief Fluent builder for PropertyDef
///
/// Provides a fluent interface for constructing PropertyDef instances.
///
/// @example
/// ```cpp
/// auto prop = PropertyDefBuilder("age")
///     .with_type(PropertyType::Int64)
///     .required()
///     .build();
/// ```
class PropertyDefBuilder {
public:
    /// @brief Construct a builder with property name
    explicit PropertyDefBuilder(std::string name) noexcept { def_.name = std::move(name); }

    /// @brief Set the property type
    PropertyDefBuilder& with_type(PropertyType type) noexcept {
        def_.type = type;
        return *this;
    }

    /// @brief Mark property as required
    PropertyDefBuilder& required(bool is_required = true) noexcept {
        def_.required = is_required;
        return *this;
    }

    /// @brief Set default value
    PropertyDefBuilder& with_default(Value value) noexcept {
        def_.default_value = value;
        return *this;
    }

    /// @brief Set target type for Handle properties
    PropertyDefBuilder& with_target_type(std::string target) noexcept {
        def_.target_type = std::move(target);
        return *this;
    }

    /// @brief Build the PropertyDef
    [[nodiscard]] PropertyDef build() const noexcept { return def_; }

private:
    PropertyDef def_;
};

}  // namespace dotvm::core::schema

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::schema::PropertyType> : std::formatter<std::string_view> {
    auto format(dotvm::core::schema::PropertyType t, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(t), ctx);
    }
};
