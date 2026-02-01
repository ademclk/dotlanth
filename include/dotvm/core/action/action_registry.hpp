#pragma once

/// @file action_registry.hpp
/// @brief DEP-005 Thread-safe registry for action definitions
///
/// Provides a thread-safe registry for managing action definitions
/// with concurrent access support.

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "action_def.hpp"
#include "action_error.hpp"
#include "dotvm/core/result.hpp"

namespace dotvm::core::action {

// ============================================================================
// ActionRegistryConfig
// ============================================================================

/// @brief Configuration for ActionRegistry
struct ActionRegistryConfig {
    /// @brief Maximum number of actions allowed in registry
    std::size_t max_actions{1024};

    /// @brief Default configuration
    [[nodiscard]] static constexpr ActionRegistryConfig defaults() noexcept {
        return ActionRegistryConfig{};
    }
};

// ============================================================================
// ActionRegistry Class
// ============================================================================

/// @brief Thread-safe registry for action definitions
class ActionRegistry {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ActionError>;

    explicit ActionRegistry(ActionRegistryConfig config = ActionRegistryConfig::defaults()) noexcept
        : config_(config) {}

    ~ActionRegistry() = default;

    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;
    ActionRegistry(ActionRegistry&&) = delete;
    ActionRegistry& operator=(ActionRegistry&&) = delete;

    [[nodiscard]] Result<void> register_action(ActionDef action) noexcept;
    [[nodiscard]] Result<void> unregister_action(const std::string& name) noexcept;
    [[nodiscard]] bool has_action(const std::string& name) const noexcept;
    [[nodiscard]] Result<std::shared_ptr<const ActionDef>>
    get_action(const std::string& name) const noexcept;
    [[nodiscard]] std::vector<std::string> action_names() const noexcept;
    [[nodiscard]] std::size_t action_count() const noexcept;
    void clear() noexcept;

private:
    ActionRegistryConfig config_;
    std::unordered_map<std::string, std::shared_ptr<ActionDef>> actions_;
    mutable std::shared_mutex mutex_;
};

}  // namespace dotvm::core::action
