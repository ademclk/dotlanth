#pragma once

/// @file migration.hpp
/// @brief DEP-003 Schema migration system
///
/// Provides version tracking and migration management for schema evolution.
/// Supports forward migrations (up) and rollback (down) with ordered execution.

#include <compare>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "dotvm/core/result.hpp"
#include "schema_error.hpp"
#include "schema_registry.hpp"

namespace dotvm::core::schema {

// ============================================================================
// MigrationVersion
// ============================================================================

/// @brief Semantic version for schema migrations
///
/// Versions are compared using standard semantic versioning rules:
/// major.minor.patch with major > minor > patch precedence.
struct MigrationVersion {
    std::uint16_t major{0};
    std::uint16_t minor{0};
    std::uint16_t patch{0};

    /// @brief Default constructor creates version 0.0.0
    constexpr MigrationVersion() noexcept = default;

    /// @brief Construct a version
    constexpr MigrationVersion(std::uint16_t maj, std::uint16_t min, std::uint16_t pat) noexcept
        : major(maj), minor(min), patch(pat) {}

    /// @brief Spaceship operator for version comparison
    [[nodiscard]] constexpr auto operator<=>(const MigrationVersion&) const noexcept = default;

    /// @brief Equality comparison
    [[nodiscard]] constexpr bool operator==(const MigrationVersion&) const noexcept = default;

    /// @brief Convert to string "major.minor.patch"
    [[nodiscard]] std::string to_string() const {
        return std::format("{}.{}.{}", major, minor, patch);
    }

    /// @brief Parse a version string "major.minor.patch"
    ///
    /// @param str The string to parse
    /// @return Result containing parsed version or error
    [[nodiscard]] static Result<MigrationVersion, SchemaError>
    parse(std::string_view str) noexcept {
        MigrationVersion v;

        // Simple parsing - find dots and extract numbers
        std::size_t pos1 = str.find('.');
        if (pos1 == std::string_view::npos) {
            return SchemaError::VersionConflict;
        }

        std::size_t pos2 = str.find('.', pos1 + 1);
        if (pos2 == std::string_view::npos) {
            return SchemaError::VersionConflict;
        }

        try {
            auto major_str = str.substr(0, pos1);
            auto minor_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
            auto patch_str = str.substr(pos2 + 1);

            // Manual conversion to avoid exceptions
            std::uint16_t maj = 0, min = 0, pat = 0;

            for (char c : major_str) {
                if (c < '0' || c > '9') {
                    return SchemaError::VersionConflict;
                }
                maj = static_cast<std::uint16_t>(maj * 10 + (c - '0'));
            }

            for (char c : minor_str) {
                if (c < '0' || c > '9') {
                    return SchemaError::VersionConflict;
                }
                min = static_cast<std::uint16_t>(min * 10 + (c - '0'));
            }

            for (char c : patch_str) {
                if (c < '0' || c > '9') {
                    return SchemaError::VersionConflict;
                }
                pat = static_cast<std::uint16_t>(pat * 10 + (c - '0'));
            }

            v.major = maj;
            v.minor = min;
            v.patch = pat;

        } catch (...) {
            return SchemaError::VersionConflict;
        }

        return v;
    }
};

// ============================================================================
// Migration
// ============================================================================

/// @brief Callback type for migration operations
///
/// Takes a mutable reference to SchemaRegistry and returns a Result.
using MigrationCallback = std::function<Result<void, SchemaError>(SchemaRegistry&)>;

/// @brief A single migration step
///
/// Migrations transform a schema from one version to another.
/// Each migration has an "up" function (forward migration) and
/// optionally a "down" function (rollback).
struct Migration {
    /// @brief Source version (migrate FROM this version)
    MigrationVersion from;

    /// @brief Target version (migrate TO this version)
    MigrationVersion to;

    /// @brief Human-readable description of the migration
    std::string description;

    /// @brief Forward migration function
    MigrationCallback up;

    /// @brief Rollback function (optional - nullptr if irreversible)
    MigrationCallback down{nullptr};

    /// @brief Check if this migration is reversible
    [[nodiscard]] bool is_reversible() const noexcept { return down != nullptr; }
};

// ============================================================================
// MigrationManagerConfig
// ============================================================================

/// @brief Configuration for MigrationManager
struct MigrationManagerConfig {
    /// @brief Maximum number of migrations to track
    std::size_t max_migrations{1024};

    /// @brief Default configuration
    [[nodiscard]] static constexpr MigrationManagerConfig defaults() noexcept {
        return MigrationManagerConfig{};
    }
};

// ============================================================================
// MigrationManager
// ============================================================================

/// @brief Manages schema migrations with version tracking
///
/// The MigrationManager maintains an ordered list of migrations and tracks
/// the current schema version. It supports forward migration to any registered
/// version and rollback to previous versions if migrations are reversible.
///
/// Thread-safe: Operations are protected by a mutex.
///
/// @example
/// ```cpp
/// MigrationManager manager;
///
/// manager.register_migration(Migration{
///     .from = {0, 0, 0},
///     .to = {1, 0, 0},
///     .description = "Add Warehouse type",
///     .up = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
///         auto type = ObjectTypeBuilder("Warehouse").build();
///         return reg.register_type(std::move(type));
///     },
///     .down = [](SchemaRegistry& reg) -> Result<void, SchemaError> {
///         return reg.unregister_type("Warehouse");
///     }
/// });
///
/// auto result = manager.migrate_to(registry, {1, 0, 0});
/// ```
class MigrationManager {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, SchemaError>;

    /// @brief Construct a MigrationManager
    explicit MigrationManager(
        MigrationManagerConfig config = MigrationManagerConfig::defaults()) noexcept
        : config_(config) {}

    // =========================================================================
    // Migration Registration
    // =========================================================================

    /// @brief Register a migration
    ///
    /// Migrations must form a valid version chain. The 'from' version must
    /// either be 0.0.0 (initial) or match a previous migration's 'to' version.
    ///
    /// @param migration The migration to register
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> register_migration(Migration migration) noexcept {
        std::lock_guard lock(mutex_);

        if (migrations_.size() >= config_.max_migrations) {
            return SchemaError::MigrationFailed;
        }

        // Validate version ordering
        if (!migrations_.empty()) {
            const auto& last = migrations_.back();
            if (migration.from != last.to) {
                return SchemaError::VersionConflict;
            }
        } else {
            // First migration should start from 0.0.0
            if (migration.from != MigrationVersion{0, 0, 0}) {
                return SchemaError::VersionConflict;
            }
        }

        // Validate target version is greater than source
        if (migration.to <= migration.from) {
            return SchemaError::VersionConflict;
        }

        migrations_.push_back(std::move(migration));
        return {};
    }

    // =========================================================================
    // Version Queries
    // =========================================================================

    /// @brief Get the current schema version
    [[nodiscard]] MigrationVersion current_version() const noexcept {
        std::lock_guard lock(mutex_);
        return current_version_;
    }

    /// @brief Get the latest registered version
    [[nodiscard]] MigrationVersion latest_version() const noexcept {
        std::lock_guard lock(mutex_);
        if (migrations_.empty()) {
            return MigrationVersion{0, 0, 0};
        }
        return migrations_.back().to;
    }

    /// @brief Get all registered migrations
    [[nodiscard]] std::vector<Migration> migrations() const noexcept {
        std::lock_guard lock(mutex_);
        return migrations_;
    }

    /// @brief Get number of registered migrations
    [[nodiscard]] std::size_t migration_count() const noexcept {
        std::lock_guard lock(mutex_);
        return migrations_.size();
    }

    // =========================================================================
    // Migration Execution
    // =========================================================================

    /// @brief Migrate to a specific version
    ///
    /// Executes all necessary migrations to reach the target version.
    /// If target is greater than current, runs forward migrations.
    /// If target is less than current, runs rollback migrations (if reversible).
    ///
    /// The target must be exactly 0.0.0 or match a registered migration's 'to' version.
    /// This prevents accidentally stopping at an intermediate version.
    ///
    /// @param registry The schema registry to migrate
    /// @param target The target version
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> migrate_to(SchemaRegistry& registry,
                                          MigrationVersion target) noexcept {
        std::lock_guard lock(mutex_);

        if (target == current_version_) {
            return {};  // Already at target version
        }

        // Validate target is a reachable version (0.0.0 or a registered migration.to)
        if (!is_valid_target_unlocked(target)) {
            return SchemaError::VersionConflict;
        }

        if (target > current_version_) {
            return migrate_forward_unlocked(registry, target);
        } else {
            return rollback_unlocked(registry, target);
        }
    }

    /// @brief Migrate to the latest version
    ///
    /// @param registry The schema registry to migrate
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> migrate_to_latest(SchemaRegistry& registry) noexcept {
        std::lock_guard lock(mutex_);

        if (migrations_.empty()) {
            return {};
        }

        return migrate_forward_unlocked(registry, migrations_.back().to);
    }

    /// @brief Rollback to a specific version
    ///
    /// Only works if all migrations between current and target are reversible.
    /// The target must be exactly 0.0.0 or match a registered migration's 'to' version.
    ///
    /// @param registry The schema registry to rollback
    /// @param target The target version to rollback to
    /// @return Result<void, SchemaError> indicating success or error
    [[nodiscard]] Result<void> rollback_to(SchemaRegistry& registry,
                                           MigrationVersion target) noexcept {
        std::lock_guard lock(mutex_);

        if (target >= current_version_) {
            return {};  // Nothing to rollback
        }

        // Validate target is a reachable version (0.0.0 or a registered migration.to)
        if (!is_valid_target_unlocked(target)) {
            return SchemaError::VersionConflict;
        }

        return rollback_unlocked(registry, target);
    }

    /// @brief Reset to initial version (0.0.0)
    ///
    /// Clears the registry and resets version tracking.
    /// This is a hard reset, not a series of rollbacks.
    void reset() noexcept {
        std::lock_guard lock(mutex_);
        current_version_ = MigrationVersion{0, 0, 0};
    }

private:
    /// @brief Check if a target version is valid (called with lock held)
    ///
    /// Valid targets are: 0.0.0 (initial state) or any registered migration's 'to' version.
    /// This prevents migration to intermediate/unregistered versions.
    [[nodiscard]] bool is_valid_target_unlocked(MigrationVersion target) const noexcept {
        // 0.0.0 is always valid (initial state)
        if (target == MigrationVersion{0, 0, 0}) {
            return true;
        }

        // Check if target matches any registered migration's 'to' version
        for (const auto& migration : migrations_) {
            if (migration.to == target) {
                return true;
            }
        }

        return false;
    }

    /// @brief Forward migration (called with lock held)
    [[nodiscard]] Result<void> migrate_forward_unlocked(SchemaRegistry& registry,
                                                        MigrationVersion target) noexcept {
        for (const auto& migration : migrations_) {
            if (migration.from >= target) {
                break;  // Past target
            }

            if (migration.from < current_version_) {
                continue;  // Already applied
            }

            if (!migration.up) {
                return SchemaError::MigrationFailed;
            }

            auto result = migration.up(registry);
            if (result.is_err()) {
                return result;
            }

            current_version_ = migration.to;
        }

        // Verify we reached the target (defense in depth)
        if (current_version_ != target) {
            return SchemaError::VersionConflict;
        }

        return {};
    }

    /// @brief Rollback (called with lock held)
    [[nodiscard]] Result<void> rollback_unlocked(SchemaRegistry& registry,
                                                 MigrationVersion target) noexcept {
        // Find migrations to rollback (in reverse order)
        for (auto it = migrations_.rbegin(); it != migrations_.rend(); ++it) {
            const auto& migration = *it;

            if (migration.to <= target) {
                break;  // Before target
            }

            if (migration.to > current_version_) {
                continue;  // Not yet applied
            }

            if (!migration.is_reversible()) {
                return SchemaError::MigrationFailed;
            }

            auto result = migration.down(registry);
            if (result.is_err()) {
                return result;
            }

            current_version_ = migration.from;
        }

        // Verify we reached the target (defense in depth)
        if (current_version_ != target) {
            return SchemaError::VersionConflict;
        }

        return {};
    }

    MigrationManagerConfig config_;
    std::vector<Migration> migrations_;
    MigrationVersion current_version_{0, 0, 0};
    mutable std::mutex mutex_;
};

}  // namespace dotvm::core::schema

// ============================================================================
// std::formatter specializations
// ============================================================================

template <>
struct std::formatter<dotvm::core::schema::MigrationVersion> : std::formatter<std::string> {
    auto format(const dotvm::core::schema::MigrationVersion& v, std::format_context& ctx) const {
        return std::formatter<std::string>::format(v.to_string(), ctx);
    }
};
