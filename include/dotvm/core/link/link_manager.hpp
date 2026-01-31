#pragma once

/// @file link_manager.hpp
/// @brief DEP-004 LinkManager for runtime relationship operations
///
/// LinkManager coordinates schema validation and storage updates for
/// object relationships stored in the StateBackend.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

#include "dotvm/core/link/link_error.hpp"
#include "dotvm/core/link/object_id.hpp"
#include "dotvm/core/result.hpp"

namespace dotvm::core::schema {
class SchemaRegistry;
}

namespace dotvm::core::state {
class StateBackend;
}

namespace dotvm::core::link {

// ============================================================================
// LinkManagerConfig
// ============================================================================

/// @brief Configuration for LinkManager
struct LinkManagerConfig {
    std::size_t max_links_per_source{10000};
    std::size_t max_traversal_depth{10};
    bool default_cascade_delete{false};
    bool auto_manage_inverse{true};

    [[nodiscard]] static constexpr LinkManagerConfig defaults() noexcept {
        return LinkManagerConfig{};
    }
};

// ============================================================================
// CascadePolicy
// ============================================================================

/// @brief Cascade policy for link removal
enum class CascadePolicy : std::uint8_t {
    None = 0,           ///< Only remove the link
    DeleteOrphans = 1,  ///< Delete targets that become orphaned (future)
};

// ============================================================================
// LinkManager
// ============================================================================

/// @brief Manage runtime object relationships stored in StateBackend
class LinkManager {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, LinkError>;

    explicit LinkManager(std::shared_ptr<schema::SchemaRegistry> schema,
                         std::shared_ptr<state::StateBackend> backend,
                         LinkManagerConfig config = LinkManagerConfig::defaults());

    /// @brief Create a link between two objects
    ///
    /// Validates ObjectIds and link definitions, enforces cardinality,
    /// and stores forward/inverse indices plus a cardinality count.
    [[nodiscard]] Result<void> create_link(ObjectId src, std::string_view source_type,
                                           std::string_view link_name, ObjectId tgt);

    /// @brief Check if a specific link exists
    [[nodiscard]] bool has_link(ObjectId src, std::string_view link_name, ObjectId tgt) const;

    /// @brief Get all targets for a link from a source
    [[nodiscard]] Result<std::vector<ObjectId>> get_links(ObjectId src,
                                                          std::string_view link_name) const;

    /// @brief Traverse a path of links starting from an object
    ///
    /// Example: traverse(order, {"customer", "address"}) returns
    /// all addresses reachable via order.customer.address
    ///
    /// @param start Starting ObjectId
    /// @param start_type Type name of the starting object
    /// @param path Vector of link names to follow in sequence
    /// @return All ObjectIds reachable at the end of the path
    [[nodiscard]] Result<std::vector<ObjectId>>
    traverse(ObjectId start, std::string_view start_type,
             std::span<const std::string_view> path) const;

    /// @brief Get all sources that point to a target via a link
    ///
    /// Uses the inverse index to find all objects pointing to the target.
    [[nodiscard]] Result<std::vector<ObjectId>> get_inverse(ObjectId tgt,
                                                            std::string_view link_name) const;

    /// @brief Get the count of links from a source
    [[nodiscard]] Result<std::uint64_t> get_link_count(ObjectId src,
                                                       std::string_view link_name) const;

    /// @brief Remove a link between two objects
    ///
    /// Removes forward/inverse indices and decrements cardinality count.
    /// If auto_manage_inverse is enabled, also removes inverse links.
    [[nodiscard]] Result<void> remove_link(ObjectId src, std::string_view source_type,
                                           std::string_view link_name, ObjectId tgt,
                                           CascadePolicy cascade = CascadePolicy::None);

    /// @brief Remove all outgoing links from an object
    ///
    /// Iterates all link types defined for the source type and removes all instances.
    [[nodiscard]] Result<void> remove_all_links_from(ObjectId src, std::string_view source_type,
                                                     CascadePolicy cascade = CascadePolicy::None);

    /// @brief Remove all incoming links to an object
    ///
    /// Uses inverse indices to find and remove all links pointing to this target.
    [[nodiscard]] Result<void> remove_all_links_to(ObjectId tgt, std::string_view target_type);

private:
    Result<void> create_link_internal(ObjectId src, std::string_view source_type,
                                      std::string_view link_name, ObjectId tgt,
                                      bool manage_inverse);
    Result<void> remove_link_internal(ObjectId src, std::string_view source_type,
                                      std::string_view link_name, ObjectId tgt,
                                      CascadePolicy cascade, bool manage_inverse);
    [[nodiscard]] Result<void> set_link_count(ObjectId src, std::string_view link_name,
                                              std::uint64_t count) const;

    std::shared_ptr<schema::SchemaRegistry> schema_;
    std::shared_ptr<state::StateBackend> backend_;
    LinkManagerConfig config_{};
    mutable std::mutex mutex_;
};

}  // namespace dotvm::core::link
