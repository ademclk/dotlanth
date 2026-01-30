#pragma once

/// @file schema_registry.hpp
/// @brief DEP-003 Thread-safe registry for object type schemas
///
/// Provides a thread-safe registry for managing object type definitions
/// with cross-type link validation and concurrent access support.

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/value.hpp"
#include "object_type.hpp"
#include "schema_error.hpp"

namespace dotvm::core::schema {

// ============================================================================
// SchemaRegistryConfig
// ============================================================================

/// @brief Configuration for SchemaRegistry
struct SchemaRegistryConfig {
    /// @brief Maximum number of types allowed in registry
    std::size_t max_types{1024};

    /// @brief Validate link targets exist when registering types
    bool validate_links_on_register{true};

    /// @brief Default configuration
    [[nodiscard]] static constexpr SchemaRegistryConfig defaults() noexcept {
        return SchemaRegistryConfig{};
    }
};

// ============================================================================
// SchemaRegistry Class
// ============================================================================

/// @brief Thread-safe registry for object type schemas
///
/// Manages registration, lookup, and validation of object type definitions.
/// Uses shared_mutex for efficient concurrent read access with exclusive
/// write access.
///
/// Types are stored as shared_ptr internally for safe concurrent access.
/// Once registered, types cannot be modified - register a new version instead.
///
/// @example
/// ```cpp
/// SchemaRegistry registry;
///
/// auto warehouse = ObjectTypeBuilder("Warehouse")
///     .add_property(PropertyDefBuilder("name")
///         .with_type(PropertyType::String)
///         .required()
///         .build())
///     .build();
///
/// auto result = registry.register_type(std::move(warehouse));
/// if (result.is_ok()) {
///     auto type = registry.get_type("Warehouse");
///     // Use type...
/// }
/// ```
class SchemaRegistry {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, SchemaError>;

    /// @brief Construct a SchemaRegistry with optional configuration
    explicit SchemaRegistry(SchemaRegistryConfig config = SchemaRegistryConfig::defaults()) noexcept
        : config_(config) {}

    /// @brief Destructor
    ~SchemaRegistry() = default;

    // Non-copyable, non-movable (shared_mutex is non-movable)
    SchemaRegistry(const SchemaRegistry&) = delete;
    SchemaRegistry& operator=(const SchemaRegistry&) = delete;
    SchemaRegistry(SchemaRegistry&&) = delete;
    SchemaRegistry& operator=(SchemaRegistry&&) = delete;

    // =========================================================================
    // Type Registration
    // =========================================================================

    /// @brief Register a new object type
    ///
    /// The type is stored by shared_ptr internally. If validate_links_on_register
    /// is enabled, all link targets must already exist in the registry.
    ///
    /// @param type The ObjectType to register
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> register_type(ObjectType type) noexcept {
        std::unique_lock lock(mutex_);

        if (type.name().empty()) {
            return SchemaError::InvalidTypeName;
        }

        if (types_.size() >= config_.max_types) {
            return SchemaError::MaxTypesExceeded;
        }

        if (types_.contains(type.name())) {
            return SchemaError::TypeAlreadyExists;
        }

        // Validate link targets if configured
        if (config_.validate_links_on_register) {
            for (const auto& link_name : type.link_names()) {
                const auto* link = type.get_link(link_name);
                if (link != nullptr) {
                    // Allow self-references
                    if (link->target_type != type.name() && !types_.contains(link->target_type)) {
                        return SchemaError::TypeNotFound;
                    }
                }
            }
        }

        // Capture name before moving type (order of evaluation is unspecified)
        std::string type_name = type.name();
        types_[type_name] = std::make_shared<ObjectType>(std::move(type));
        return {};
    }

    /// @brief Unregister an object type
    ///
    /// Fails if other types have links pointing to this type.
    ///
    /// @param name The type name to unregister
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> unregister_type(const std::string& name) noexcept {
        std::unique_lock lock(mutex_);

        auto it = types_.find(name);
        if (it == types_.end()) {
            return SchemaError::TypeNotFound;
        }

        // Check if any other type links to this type
        for (const auto& [type_name, type_ptr] : types_) {
            if (type_name == name) {
                continue;
            }
            for (const auto& link_name : type_ptr->link_names()) {
                const auto* link = type_ptr->get_link(link_name);
                if (link != nullptr && link->target_type == name) {
                    return SchemaError::CardinalityViolation;  // Type is referenced
                }
            }
        }

        types_.erase(it);
        return {};
    }

    // =========================================================================
    // Type Lookup
    // =========================================================================

    /// @brief Check if a type exists
    [[nodiscard]] bool has_type(const std::string& name) const noexcept {
        std::shared_lock lock(mutex_);
        return types_.contains(name);
    }

    /// @brief Get a type by name
    ///
    /// Returns a shared_ptr to the type for safe concurrent access.
    ///
    /// @param name The type name to look up
    /// @return Result containing shared_ptr to type or TypeNotFound error
    [[nodiscard]] Result<std::shared_ptr<const ObjectType>>
    get_type(const std::string& name) const noexcept {
        std::shared_lock lock(mutex_);

        auto it = types_.find(name);
        if (it == types_.end()) {
            return SchemaError::TypeNotFound;
        }
        return std::const_pointer_cast<const ObjectType>(it->second);
    }

    /// @brief Get all registered type names
    [[nodiscard]] std::vector<std::string> type_names() const noexcept {
        std::shared_lock lock(mutex_);

        std::vector<std::string> names;
        names.reserve(types_.size());
        for (const auto& [name, _] : types_) {
            names.push_back(name);
        }
        return names;
    }

    /// @brief Get the number of registered types
    [[nodiscard]] std::size_t type_count() const noexcept {
        std::shared_lock lock(mutex_);
        return types_.size();
    }

    // =========================================================================
    // Validation
    // =========================================================================

    /// @brief Validate an object against a registered type
    ///
    /// @param type_name The type to validate against
    /// @param data The property values to validate
    /// @return Result<void, SchemaError> indicating success or validation error
    [[nodiscard]] Result<void>
    validate_object(const std::string& type_name,
                    const std::unordered_map<std::string, Value>& data) const noexcept {
        auto type_result = get_type(type_name);
        if (type_result.is_err()) {
            return type_result.error();
        }

        return type_result.value()->validate_data(data);
    }

    /// @brief Validate links between objects
    ///
    /// Verifies that a link value (Handle) points to a valid object type.
    /// This is a schema-level validation only - actual object existence
    /// must be checked separately.
    ///
    /// @param source_type_name The source type name
    /// @param link_name The link name on the source type
    /// @param target_type_name The target type name (for validation)
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> validate_link(const std::string& source_type_name,
                                             const std::string& link_name,
                                             const std::string& target_type_name) const noexcept {
        std::shared_lock lock(mutex_);

        auto source_it = types_.find(source_type_name);
        if (source_it == types_.end()) {
            return SchemaError::TypeNotFound;
        }

        const auto* link = source_it->second->get_link(link_name);
        if (link == nullptr) {
            return SchemaError::LinkNotFound;
        }

        if (link->target_type != target_type_name) {
            return SchemaError::InvalidPropertyType;
        }

        if (!types_.contains(target_type_name)) {
            return SchemaError::TypeNotFound;
        }

        return {};
    }

    // =========================================================================
    // Utilities
    // =========================================================================

    /// @brief Clear all registered types
    void clear() noexcept {
        std::unique_lock lock(mutex_);
        types_.clear();
    }

private:
    SchemaRegistryConfig config_;
    std::unordered_map<std::string, std::shared_ptr<ObjectType>> types_;
    mutable std::shared_mutex mutex_;
};

}  // namespace dotvm::core::schema
