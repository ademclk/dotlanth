#pragma once

/// @file object_type.hpp
/// @brief DEP-003 ObjectType class for object type definitions
///
/// Provides the ObjectType class which defines an object type's properties
/// and links. ObjectType instances are immutable after construction for
/// thread-safety when stored in SchemaRegistry.

#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/value.hpp"
#include "link_def.hpp"
#include "property_type.hpp"
#include "schema_error.hpp"
#include "validator.hpp"

namespace dotvm::core::schema {

// ============================================================================
// ObjectTypeConfig
// ============================================================================

/// @brief Configuration limits for ObjectType
struct ObjectTypeConfig {
    /// @brief Maximum number of properties per type
    std::size_t max_properties{256};

    /// @brief Maximum number of links per type
    std::size_t max_links{64};

    /// @brief Maximum number of validators per property
    std::size_t max_validators_per_property{16};

    /// @brief Default configuration
    [[nodiscard]] static constexpr ObjectTypeConfig defaults() noexcept {
        return ObjectTypeConfig{};
    }
};

// ============================================================================
// ValidatedProperty
// ============================================================================

/// @brief A property definition with associated validators
struct ValidatedProperty {
    /// @brief The property definition
    PropertyDef definition;

    /// @brief Validators to apply to property values
    std::vector<Validator> validators;
};

// ============================================================================
// ObjectType Class
// ============================================================================

/// @brief Defines an object type with properties and links
///
/// ObjectType is immutable after construction. Use ObjectTypeBuilder to
/// construct instances. This immutability ensures thread-safety when
/// ObjectType instances are stored in SchemaRegistry.
///
/// @example
/// ```cpp
/// auto warehouse_type = ObjectTypeBuilder("Warehouse")
///     .add_property(PropertyDefBuilder("name")
///         .with_type(PropertyType::String)
///         .required()
///         .build())
///     .add_property(PropertyDefBuilder("capacity")
///         .with_type(PropertyType::Int64)
///         .build(),
///         {range(0, 1000000)})
///     .build();
/// ```
class ObjectType {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, SchemaError>;

    /// @brief Construct an empty ObjectType with a name
    explicit ObjectType(std::string name) noexcept : name_(std::move(name)) {}

    /// @brief Get the type name
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    // =========================================================================
    // Property Access (const only - ObjectType is immutable)
    // =========================================================================

    /// @brief Check if a property exists
    [[nodiscard]] bool has_property(std::string_view name) const noexcept {
        return properties_.contains(std::string(name));
    }

    /// @brief Get a property definition
    /// @return Pointer to ValidatedProperty or nullptr if not found
    [[nodiscard]] const ValidatedProperty* get_property(std::string_view name) const noexcept {
        auto it = properties_.find(std::string(name));
        if (it == properties_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    /// @brief Get all property names
    [[nodiscard]] std::vector<std::string> property_names() const noexcept {
        std::vector<std::string> names;
        names.reserve(properties_.size());
        for (const auto& [name, _] : properties_) {
            names.push_back(name);
        }
        return names;
    }

    /// @brief Get number of properties
    [[nodiscard]] std::size_t property_count() const noexcept { return properties_.size(); }

    // =========================================================================
    // Link Access (const only - ObjectType is immutable)
    // =========================================================================

    /// @brief Check if a link exists
    [[nodiscard]] bool has_link(std::string_view name) const noexcept {
        return links_.contains(std::string(name));
    }

    /// @brief Get a link definition
    /// @return Pointer to LinkDef or nullptr if not found
    [[nodiscard]] const LinkDef* get_link(std::string_view name) const noexcept {
        auto it = links_.find(std::string(name));
        if (it == links_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    /// @brief Get all link names
    [[nodiscard]] std::vector<std::string> link_names() const noexcept {
        std::vector<std::string> names;
        names.reserve(links_.size());
        for (const auto& [name, _] : links_) {
            names.push_back(name);
        }
        return names;
    }

    /// @brief Get number of links
    [[nodiscard]] std::size_t link_count() const noexcept { return links_.size(); }

    // =========================================================================
    // Validation
    // =========================================================================

    /// @brief Validate a data map against this type's schema
    ///
    /// Checks that:
    /// - All required properties are present and not nil
    /// - All provided values match their property types
    /// - All validators pass for each property
    ///
    /// @param data Map of property names to values
    /// @return Result<void, SchemaError> indicating success or first validation error
    [[nodiscard]] Result<void>
    validate_data(const std::unordered_map<std::string, Value>& data) const noexcept {
        // Check all properties
        for (const auto& [prop_name, validated_prop] : properties_) {
            const auto& prop_def = validated_prop.definition;
            auto it = data.find(prop_name);

            Value value = Value::nil();
            if (it != data.end()) {
                value = it->second;
            } else if (prop_def.default_value.has_value()) {
                value = *prop_def.default_value;
            }

            // Check required constraint
            if (prop_def.required && value.is_nil()) {
                return SchemaError::RequiredPropertyMissing;
            }

            // Check type compatibility
            if (!value.is_nil() && !is_compatible(value, prop_def.type)) {
                return SchemaError::InvalidPropertyType;
            }

            // Run validators
            auto validation_result = validate_value(value, validated_prop.validators);
            if (validation_result.is_err()) {
                return validation_result.error();
            }
        }

        return {};
    }

    /// @brief Validate a single property value
    ///
    /// @param property_name The property to validate against
    /// @param value The value to validate
    /// @return Result<void, SchemaError> indicating success or validation error
    [[nodiscard]] Result<void> validate_property(std::string_view property_name,
                                                 Value value) const noexcept {
        const auto* validated_prop = get_property(property_name);
        if (validated_prop == nullptr) {
            return SchemaError::PropertyNotFound;
        }

        const auto& prop_def = validated_prop->definition;

        // Check required constraint
        if (prop_def.required && value.is_nil()) {
            return SchemaError::RequiredPropertyMissing;
        }

        // Check type compatibility
        if (!value.is_nil() && !is_compatible(value, prop_def.type)) {
            return SchemaError::InvalidPropertyType;
        }

        // Run validators
        return validate_value(value, validated_prop->validators);
    }

private:
    friend class ObjectTypeBuilder;

    std::string name_;
    std::unordered_map<std::string, ValidatedProperty> properties_;
    std::unordered_map<std::string, LinkDef> links_;
};

// ============================================================================
// ObjectTypeBuilder
// ============================================================================

/// @brief Fluent builder for ObjectType
///
/// Builds immutable ObjectType instances with properties and links.
class ObjectTypeBuilder {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, SchemaError>;

    /// @brief Construct a builder with type name
    explicit ObjectTypeBuilder(std::string name,
                               ObjectTypeConfig config = ObjectTypeConfig::defaults()) noexcept
        : type_(std::move(name)), config_(config) {}

    /// @brief Add a property without validators
    ///
    /// Returns Result<void> - check for errors before continuing.
    /// For fluent chaining without error checks, use try_add_property().
    Result<void> add_property(PropertyDef property) noexcept {
        return add_property(std::move(property), {});
    }

    /// @brief Add a property with validators
    ///
    /// Returns Result<void> - check for errors before continuing.
    Result<void> add_property(PropertyDef property, std::vector<Validator> validators) noexcept {
        if (type_.properties_.size() >= config_.max_properties) {
            return SchemaError::MaxTypesExceeded;  // Reusing error for property limit
        }

        if (property.name.empty()) {
            return SchemaError::InvalidTypeName;  // Reusing for invalid property name
        }

        if (type_.properties_.contains(property.name)) {
            return SchemaError::PropertyAlreadyExists;
        }

        if (validators.size() > config_.max_validators_per_property) {
            return SchemaError::ValidationFailed;  // Too many validators
        }

        // Auto-add RequiredValidator if property is required
        bool has_required = false;
        for (const auto& v : validators) {
            if (std::holds_alternative<RequiredValidator>(v)) {
                has_required = true;
                break;
            }
        }
        if (property.required && !has_required) {
            validators.push_back(RequiredValidator{});
        }

        // Capture name before moving property (order of evaluation is unspecified)
        std::string prop_name = property.name;
        type_.properties_[prop_name] = ValidatedProperty{.definition = std::move(property),
                                                         .validators = std::move(validators)};

        return {};
    }

    /// @brief Add a property without validators (fluent, no error check)
    ///
    /// Ignores errors - use when you're confident the operation will succeed.
    /// For error checking, use add_property() instead.
    ObjectTypeBuilder& try_add_property(PropertyDef property) noexcept {
        (void)add_property(std::move(property), {});
        return *this;
    }

    /// @brief Add a property with validators (fluent, no error check)
    ///
    /// Ignores errors - use when you're confident the operation will succeed.
    ObjectTypeBuilder& try_add_property(PropertyDef property,
                                        std::vector<Validator> validators) noexcept {
        (void)add_property(std::move(property), std::move(validators));
        return *this;
    }

    /// @brief Add a link
    ///
    /// Returns Result<void> - check for errors before continuing.
    Result<void> add_link(LinkDef link) noexcept {
        if (type_.links_.size() >= config_.max_links) {
            return SchemaError::MaxTypesExceeded;  // Reusing error for link limit
        }

        if (link.name.empty()) {
            return SchemaError::InvalidTypeName;  // Reusing for invalid link name
        }

        if (type_.links_.contains(link.name)) {
            return SchemaError::PropertyAlreadyExists;  // Reusing for duplicate link
        }

        // Ensure source_type matches this type
        link.source_type = type_.name_;
        type_.links_[link.name] = std::move(link);

        return {};
    }

    /// @brief Add a link (fluent, no error check)
    ///
    /// Ignores errors - use when you're confident the operation will succeed.
    ObjectTypeBuilder& try_add_link(LinkDef link) noexcept {
        (void)add_link(std::move(link));
        return *this;
    }

    /// @brief Build the ObjectType
    [[nodiscard]] ObjectType build() && noexcept { return std::move(type_); }

    /// @brief Build a copy of the ObjectType
    [[nodiscard]] ObjectType build() const& noexcept { return type_; }

private:
    ObjectType type_;
    ObjectTypeConfig config_;
};

}  // namespace dotvm::core::schema
