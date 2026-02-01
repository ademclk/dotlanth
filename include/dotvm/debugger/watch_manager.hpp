#pragma once

/// @file watch_manager.hpp
/// @brief TOOL-011 Debug Client - Memory watchpoint management
///
/// Provides memory watchpoints that trigger when watched memory changes.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

#include "dotvm/core/memory.hpp"

namespace dotvm::debugger {

/// @brief Type of memory access to watch for
enum class WatchType : std::uint8_t {
    Write = 0,     ///< Break on write
    Read = 1,      ///< Break on read
    ReadWrite = 2  ///< Break on read or write
};

/// @brief Convert watch type to string
[[nodiscard]] constexpr const char* to_string(WatchType type) noexcept {
    switch (type) {
        case WatchType::Write:
            return "write";
        case WatchType::Read:
            return "read";
        case WatchType::ReadWrite:
            return "read/write";
    }
    return "unknown";
}

/// @brief A memory watchpoint
struct Watchpoint {
    std::uint32_t id;                  ///< Unique watchpoint ID
    core::Handle handle;               ///< Memory allocation handle
    std::size_t offset;                ///< Offset within allocation
    std::size_t size;                  ///< Number of bytes to watch
    WatchType type{WatchType::Write};  ///< Type of access to watch
    bool enabled{true};                ///< Whether watchpoint is active
    std::uint32_t hit_count{0};        ///< Number of times triggered
    std::string comment;               ///< User comment/description

    /// Cached previous value for change detection
    std::vector<std::uint8_t> previous_value;
};

/// @brief Result of checking watchpoints
struct WatchCheckResult {
    bool triggered{false};            ///< Whether a watchpoint was triggered
    std::optional<std::uint32_t> id;  ///< ID of watchpoint that triggered (if any)
};

/// @brief Manages memory watchpoints
///
/// Watchpoints monitor memory regions for changes. On each check,
/// the current memory value is compared to the cached previous value.
class WatchManager {
public:
    /// @brief Memory reader function type
    using MemoryReader =
        std::function<std::vector<std::uint8_t>(core::Handle, std::size_t, std::size_t)>;

    /// @brief Construct the watch manager
    WatchManager() = default;

    /// @brief Set a watchpoint
    /// @param handle Memory allocation handle
    /// @param offset Offset within allocation
    /// @param size Number of bytes to watch
    /// @param type Type of access to watch (default: write)
    /// @return The ID of the new watchpoint
    std::uint32_t set(core::Handle handle, std::size_t offset, std::size_t size,
                      WatchType type = WatchType::Write);

    /// @brief Remove a watchpoint by ID
    /// @param id The watchpoint ID to remove
    /// @return true if the watchpoint existed and was removed
    bool remove(std::uint32_t id);

    /// @brief Enable a watchpoint
    /// @param id The watchpoint ID to enable
    /// @return true if the watchpoint exists
    bool enable(std::uint32_t id);

    /// @brief Disable a watchpoint
    /// @param id The watchpoint ID to disable
    /// @return true if the watchpoint exists
    bool disable(std::uint32_t id);

    /// @brief Get a watchpoint by ID
    /// @param id The watchpoint ID
    /// @return Pointer to watchpoint or nullptr if not found
    [[nodiscard]] const Watchpoint* get(std::uint32_t id) const;

    /// @brief Get all watchpoints
    [[nodiscard]] std::vector<const Watchpoint*> list() const;

    /// @brief Check all watchpoints for changes
    ///
    /// Reads current memory values and compares to cached previous values.
    /// If any watched memory has changed, returns the triggered watchpoint.
    ///
    /// @param reader Function to read memory
    /// @return Result indicating if a watchpoint was triggered
    [[nodiscard]] WatchCheckResult check(const MemoryReader& reader);

    /// @brief Update cached values for all watchpoints
    ///
    /// Call this after execution to capture current memory state.
    ///
    /// @param reader Function to read memory
    void update_cached_values(const MemoryReader& reader);

    /// @brief Clear all watchpoints
    void clear();

    /// @brief Get total number of watchpoints
    [[nodiscard]] std::size_t count() const noexcept { return watchpoints_.size(); }

private:
    std::uint32_t next_id_{1};
    std::unordered_map<std::uint32_t, Watchpoint> watchpoints_;
};

}  // namespace dotvm::debugger
