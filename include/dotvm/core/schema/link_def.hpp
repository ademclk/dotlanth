#pragma once

/// @file link_def.hpp
/// @brief DEP-003 Link definitions for Object Type Schema
///
/// Defines link cardinality and LinkDef for relationships between object types.

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace dotvm::core::schema {

// ============================================================================
// Cardinality Enum
// ============================================================================

/// @brief Cardinality constraint for links between object types
///
/// Defines how many target objects a source object can reference:
/// - OneToOne: Source references exactly one target (1:1)
/// - OneToMany: Source references zero or more targets (1:N)
/// - ManyToMany: Multiple sources can reference multiple targets (M:N)
enum class Cardinality : std::uint8_t {
    OneToOne = 0,    ///< Single reference (max 1 target)
    OneToMany = 1,   ///< Multiple references (unbounded targets)
    ManyToMany = 2,  ///< Bidirectional multiple references
};

/// @brief Convert Cardinality to human-readable string
[[nodiscard]] constexpr std::string_view to_string(Cardinality cardinality) noexcept {
    switch (cardinality) {
        case Cardinality::OneToOne:
            return "OneToOne";
        case Cardinality::OneToMany:
            return "OneToMany";
        case Cardinality::ManyToMany:
            return "ManyToMany";
    }
    return "Unknown";
}

/// @brief Get the maximum number of targets for a cardinality
///
/// @param cardinality The cardinality to check
/// @return Maximum targets (0 = unlimited)
[[nodiscard]] constexpr std::size_t max_targets(Cardinality cardinality) noexcept {
    switch (cardinality) {
        case Cardinality::OneToOne:
            return 1;
        case Cardinality::OneToMany:
        case Cardinality::ManyToMany:
            return 0;  // 0 = unlimited
    }
    return 0;
}

// ============================================================================
// LinkDef Struct
// ============================================================================

/// @brief Definition of a link between object types
///
/// Links define relationships between object types with cardinality constraints.
/// For example, an Order type might have a "customer" link to a Customer type
/// with OneToOne cardinality.
struct LinkDef {
    /// @brief Link name (must be non-empty and unique within type)
    std::string name;

    /// @brief Source type name (the type this link is defined on)
    std::string source_type;

    /// @brief Target type name (the type this link points to)
    std::string target_type;

    /// @brief Cardinality constraint
    Cardinality cardinality{Cardinality::OneToOne};

    /// @brief Whether this link is required (must have at least one target)
    bool required{false};

    /// @brief Name of the inverse link on the target type (empty = no inverse)
    std::string inverse_link{};

    /// @brief Equality comparison
    [[nodiscard]] bool operator==(const LinkDef& other) const noexcept {
        return name == other.name && source_type == other.source_type &&
               target_type == other.target_type && cardinality == other.cardinality &&
               required == other.required && inverse_link == other.inverse_link;
    }
};

// ============================================================================
// LinkDefBuilder
// ============================================================================

/// @brief Fluent builder for LinkDef
///
/// Provides a fluent interface for constructing LinkDef instances.
///
/// @example
/// ```cpp
/// auto link = LinkDefBuilder("customer")
///     .from("Order")
///     .to("Customer")
///     .with_cardinality(Cardinality::OneToOne)
///     .required()
///     .build();
/// ```
class LinkDefBuilder {
public:
    /// @brief Construct a builder with link name
    explicit LinkDefBuilder(std::string name) noexcept { def_.name = std::move(name); }

    /// @brief Set the source type
    LinkDefBuilder& from(std::string source) noexcept {
        def_.source_type = std::move(source);
        return *this;
    }

    /// @brief Set the target type
    LinkDefBuilder& to(std::string target) noexcept {
        def_.target_type = std::move(target);
        return *this;
    }

    /// @brief Set the cardinality
    LinkDefBuilder& with_cardinality(Cardinality cardinality) noexcept {
        def_.cardinality = cardinality;
        return *this;
    }

    /// @brief Mark link as required
    LinkDefBuilder& required(bool is_required = true) noexcept {
        def_.required = is_required;
        return *this;
    }

    /// @brief Set the inverse link name
    LinkDefBuilder& with_inverse(std::string inverse) noexcept {
        def_.inverse_link = std::move(inverse);
        return *this;
    }

    /// @brief Build the LinkDef
    [[nodiscard]] LinkDef build() const noexcept { return def_; }

private:
    LinkDef def_;
};

}  // namespace dotvm::core::schema

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::schema::Cardinality> : std::formatter<std::string_view> {
    auto format(dotvm::core::schema::Cardinality c, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(c), ctx);
    }
};
