#pragma once

/// @file lock_file.hpp
/// @brief PRD-007 Lock file for reproducible package installations
///
/// Binary format for storing locked package versions with checksums.
/// The lock file ensures reproducible builds by recording exact versions
/// and checksums of all installed packages.
///
/// Binary Format:
/// ```
/// Header (16 bytes):
///   [0-3]   Magic: "DPKL" (0x444F544C)
///   [4-5]   Format version: uint16 (1)
///   [6-7]   Reserved: uint16 (0)
///   [8-11]  Package count: uint32
///   [12-15] Timestamp: uint32 (unix seconds)
///
/// Per Package (variable):
///   [0-1]   Name length: uint16
///   [2-N]   Name: UTF-8 bytes
///   [N+0-3] Major: uint32
///   [N+4-7] Minor: uint32
///   [N+8-11] Patch: uint32
///   [N+12-43] Checksum: SHA-256 (32 bytes)
///   [N+44-45] Dependency count: uint16
///   For each dependency:
///     [0-1] Name length: uint16
///     [2-M] Name: UTF-8 bytes
///
/// Footer (4 bytes):
///   CRC32 of Header + Packages
/// ```

#include <cstdint>
#include <filesystem>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/pkg/package.hpp"
#include "dotvm/pkg/package_error.hpp"

namespace dotvm::pkg {

/// @brief Lock file format constants
struct LockFileConstants {
    /// @brief Magic bytes: "DPKL" (Dot Package Lock)
    static constexpr std::uint32_t MAGIC = 0x4C4B5044;  // "DPKL" in little-endian

    /// @brief Current format version
    static constexpr std::uint16_t VERSION = 1;

    /// @brief Header size in bytes
    static constexpr std::size_t HEADER_SIZE = 16;

    /// @brief Footer size (CRC32)
    static constexpr std::size_t FOOTER_SIZE = 4;

    /// @brief Maximum package name length
    static constexpr std::size_t MAX_NAME_LENGTH = 256;

    /// @brief Maximum dependencies per package
    static constexpr std::size_t MAX_DEPENDENCIES = 1024;
};

/// @brief Lock file for reproducible package installations
///
/// Stores exact versions and checksums of all installed packages,
/// enabling reproducible builds across machines and time.
///
/// @example
/// ```cpp
/// LockFile lock;
/// lock.add_package(LockedPackage{
///     .name = "mylib",
///     .version = Version{1, 0, 0, {}},
///     .checksum = compute_checksum(package_data),
///     .dependencies = {"otherlib"}
/// });
/// lock.save("dotpkg.lock");
/// ```
class LockFile {
public:
    /// @brief Construct an empty lock file
    LockFile() noexcept = default;

    // =========================================================================
    // Factory Methods
    // =========================================================================

    /// @brief Load a lock file from disk
    /// @param path Path to the lock file
    /// @return LockFile on success, PackageError on failure
    [[nodiscard]] static core::Result<LockFile, PackageError> load(
        const std::filesystem::path& path) noexcept;

    /// @brief Parse a lock file from binary data
    /// @param data Binary lock file data
    /// @return LockFile on success, PackageError on failure
    [[nodiscard]] static core::Result<LockFile, PackageError> parse(
        std::span<const std::uint8_t> data) noexcept;

    // =========================================================================
    // Persistence
    // =========================================================================

    /// @brief Save the lock file to disk
    /// @param path Path to save to
    /// @return void on success, PackageError on failure
    [[nodiscard]] core::Result<void, PackageError> save(
        const std::filesystem::path& path) const noexcept;

    /// @brief Serialize to binary format
    /// @return Binary lock file data
    [[nodiscard]] std::vector<std::uint8_t> serialize() const noexcept;

    // =========================================================================
    // Package Management
    // =========================================================================

    /// @brief Add or update a locked package
    /// @param package The package to add/update
    void add_package(LockedPackage package) noexcept;

    /// @brief Remove a package from the lock file
    /// @param name Package name to remove
    /// @return true if removed, false if not found
    bool remove_package(std::string_view name) noexcept;

    /// @brief Get a locked package by name
    /// @param name Package name
    /// @return Pointer to package, or nullptr if not found
    [[nodiscard]] const LockedPackage* get_package(std::string_view name) const noexcept;

    /// @brief Check if a package is locked
    [[nodiscard]] bool has_package(std::string_view name) const noexcept;

    /// @brief Get all locked packages
    [[nodiscard]] const std::vector<LockedPackage>& packages() const noexcept { return packages_; }

    /// @brief Get the number of locked packages
    [[nodiscard]] std::size_t package_count() const noexcept { return packages_.size(); }

    // =========================================================================
    // Validation
    // =========================================================================

    /// @brief Verify integrity of the lock file
    [[nodiscard]] bool verify_integrity() const noexcept;

    /// @brief Check if lock file matches a manifest
    /// @param manifest The manifest to compare against
    /// @return true if all manifest dependencies are locked
    [[nodiscard]] bool matches_manifest(const PackageManifest& manifest) const noexcept;

    // =========================================================================
    // Metadata
    // =========================================================================

    /// @brief Get the lock file timestamp (when last modified)
    [[nodiscard]] std::uint32_t timestamp() const noexcept { return timestamp_; }

    /// @brief Set the timestamp
    void set_timestamp(std::uint32_t ts) noexcept { timestamp_ = ts; }

    /// @brief Clear all packages
    void clear() noexcept;

private:
    std::vector<LockedPackage> packages_;
    std::uint32_t timestamp_{0};
};

// ============================================================================
// CRC32 Utility
// ============================================================================

/// @brief Compute CRC32 checksum
/// @param data Data to checksum
/// @return CRC32 value
[[nodiscard]] std::uint32_t compute_crc32(std::span<const std::uint8_t> data) noexcept;

}  // namespace dotvm::pkg
