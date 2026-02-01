#pragma once

/// @file package_error.hpp
/// @brief PRD-007 Package manager error codes
///
/// Error codes for the dotpkg package manager. Uses range 1-95 to leave
/// room for CLI/system exit codes.

#include <cstdint>
#include <format>
#include <string_view>

namespace dotvm::pkg {

/// @brief Error codes for package manager operations
///
/// Error codes are grouped by category:
/// - 1-10:  Version parsing errors
/// - 11-20: Constraint errors
/// - 21-30: Manifest/package errors
/// - 31-40: Lock file errors
/// - 41-50: Cache errors
/// - 51-60: Registry errors
/// - 61-70: Dependency resolution errors
/// - 71-80: I/O errors
/// - 81-95: Reserved for future use
enum class PackageError : std::uint8_t {
    // Version parsing errors (1-10)
    InvalidVersionFormat = 1,  ///< Version string cannot be parsed
    EmptyVersion = 2,          ///< Empty version string
    InvalidMajorVersion = 3,   ///< Major version component invalid
    InvalidMinorVersion = 4,   ///< Minor version component invalid
    InvalidPatchVersion = 5,   ///< Patch version component invalid
    InvalidPrerelease = 6,     ///< Prerelease identifier invalid
    VersionOverflow = 7,       ///< Version number too large

    // Constraint errors (11-20)
    InvalidConstraint = 11,        ///< Constraint string cannot be parsed
    EmptyConstraint = 12,          ///< Empty constraint string
    UnsupportedOperator = 13,      ///< Constraint operator not supported
    IncompatibleConstraints = 14,  ///< Constraints cannot be satisfied together

    // Manifest/package errors (21-30)
    ManifestNotFound = 21,       ///< dotpkg.json not found
    InvalidManifest = 22,        ///< Manifest JSON is invalid
    MissingPackageName = 23,     ///< Package name not specified
    MissingPackageVersion = 24,  ///< Package version not specified
    InvalidPackageName = 25,     ///< Package name contains invalid characters
    PackageNotFound = 26,        ///< Package not found in registry or cache
    PackageAlreadyExists = 27,   ///< Package already installed

    // Lock file errors (31-40)
    LockFileNotFound = 31,         ///< dotpkg.lock not found
    InvalidLockFile = 32,          ///< Lock file is corrupted or invalid
    LockFileMismatch = 33,         ///< Lock file doesn't match manifest
    LockFileVersionMismatch = 34,  ///< Lock file format version not supported
    ChecksumMismatch = 35,         ///< Package checksum doesn't match lock file

    // Cache errors (41-50)
    CacheNotFound = 41,      ///< Cache directory not found
    CacheCorrupted = 42,     ///< Cache is corrupted
    CacheWriteError = 43,    ///< Failed to write to cache
    CacheReadError = 44,     ///< Failed to read from cache
    CacheCleanupError = 45,  ///< Failed to clean up cache

    // Registry errors (51-60)
    RegistryNotFound = 51,    ///< Registry not found
    RegistryCorrupted = 52,   ///< Registry is corrupted
    RegistryWriteError = 53,  ///< Failed to write to registry
    RegistryReadError = 54,   ///< Failed to read from registry

    // Dependency resolution errors (61-70)
    DependencyCycle = 61,         ///< Circular dependency detected
    UnresolvableDependency = 62,  ///< Cannot satisfy dependency
    ConflictingVersions = 63,     ///< Multiple versions required for same package
    MaxDepthExceeded = 64,        ///< Dependency tree too deep
    ResolutionTimeout = 65,       ///< Dependency resolution took too long

    // I/O errors (71-80)
    FileNotFound = 71,          ///< File not found
    FileReadError = 72,         ///< Failed to read file
    FileWriteError = 73,        ///< Failed to write file
    DirectoryCreateError = 74,  ///< Failed to create directory
    DirectoryNotFound = 75,     ///< Directory not found
    PermissionDenied = 76,      ///< Permission denied
    PathTooLong = 77,           ///< Path exceeds system limits

    // Archive errors (81-90)
    InvalidArchive = 81,       ///< Archive format invalid
    ArchiveExtractError = 82,  ///< Failed to extract archive
    ArchiveCreateError = 83,   ///< Failed to create archive
};

/// @brief Convert PackageError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(PackageError error) noexcept {
    switch (error) {
        // Version parsing errors
        case PackageError::InvalidVersionFormat:
            return "InvalidVersionFormat";
        case PackageError::EmptyVersion:
            return "EmptyVersion";
        case PackageError::InvalidMajorVersion:
            return "InvalidMajorVersion";
        case PackageError::InvalidMinorVersion:
            return "InvalidMinorVersion";
        case PackageError::InvalidPatchVersion:
            return "InvalidPatchVersion";
        case PackageError::InvalidPrerelease:
            return "InvalidPrerelease";
        case PackageError::VersionOverflow:
            return "VersionOverflow";

        // Constraint errors
        case PackageError::InvalidConstraint:
            return "InvalidConstraint";
        case PackageError::EmptyConstraint:
            return "EmptyConstraint";
        case PackageError::UnsupportedOperator:
            return "UnsupportedOperator";
        case PackageError::IncompatibleConstraints:
            return "IncompatibleConstraints";

        // Manifest/package errors
        case PackageError::ManifestNotFound:
            return "ManifestNotFound";
        case PackageError::InvalidManifest:
            return "InvalidManifest";
        case PackageError::MissingPackageName:
            return "MissingPackageName";
        case PackageError::MissingPackageVersion:
            return "MissingPackageVersion";
        case PackageError::InvalidPackageName:
            return "InvalidPackageName";
        case PackageError::PackageNotFound:
            return "PackageNotFound";
        case PackageError::PackageAlreadyExists:
            return "PackageAlreadyExists";

        // Lock file errors
        case PackageError::LockFileNotFound:
            return "LockFileNotFound";
        case PackageError::InvalidLockFile:
            return "InvalidLockFile";
        case PackageError::LockFileMismatch:
            return "LockFileMismatch";
        case PackageError::LockFileVersionMismatch:
            return "LockFileVersionMismatch";
        case PackageError::ChecksumMismatch:
            return "ChecksumMismatch";

        // Cache errors
        case PackageError::CacheNotFound:
            return "CacheNotFound";
        case PackageError::CacheCorrupted:
            return "CacheCorrupted";
        case PackageError::CacheWriteError:
            return "CacheWriteError";
        case PackageError::CacheReadError:
            return "CacheReadError";
        case PackageError::CacheCleanupError:
            return "CacheCleanupError";

        // Registry errors
        case PackageError::RegistryNotFound:
            return "RegistryNotFound";
        case PackageError::RegistryCorrupted:
            return "RegistryCorrupted";
        case PackageError::RegistryWriteError:
            return "RegistryWriteError";
        case PackageError::RegistryReadError:
            return "RegistryReadError";

        // Dependency resolution errors
        case PackageError::DependencyCycle:
            return "DependencyCycle";
        case PackageError::UnresolvableDependency:
            return "UnresolvableDependency";
        case PackageError::ConflictingVersions:
            return "ConflictingVersions";
        case PackageError::MaxDepthExceeded:
            return "MaxDepthExceeded";
        case PackageError::ResolutionTimeout:
            return "ResolutionTimeout";

        // I/O errors
        case PackageError::FileNotFound:
            return "FileNotFound";
        case PackageError::FileReadError:
            return "FileReadError";
        case PackageError::FileWriteError:
            return "FileWriteError";
        case PackageError::DirectoryCreateError:
            return "DirectoryCreateError";
        case PackageError::DirectoryNotFound:
            return "DirectoryNotFound";
        case PackageError::PermissionDenied:
            return "PermissionDenied";
        case PackageError::PathTooLong:
            return "PathTooLong";

        // Archive errors
        case PackageError::InvalidArchive:
            return "InvalidArchive";
        case PackageError::ArchiveExtractError:
            return "ArchiveExtractError";
        case PackageError::ArchiveCreateError:
            return "ArchiveCreateError";
    }
    return "Unknown";
}

/// @brief Get a user-friendly error message
[[nodiscard]] constexpr std::string_view error_message(PackageError error) noexcept {
    switch (error) {
        // Version parsing errors
        case PackageError::InvalidVersionFormat:
            return "Version string is not valid semantic version format (MAJOR.MINOR.PATCH)";
        case PackageError::EmptyVersion:
            return "Version string is empty";
        case PackageError::InvalidMajorVersion:
            return "Major version component is not a valid number";
        case PackageError::InvalidMinorVersion:
            return "Minor version component is not a valid number";
        case PackageError::InvalidPatchVersion:
            return "Patch version component is not a valid number";
        case PackageError::InvalidPrerelease:
            return "Prerelease identifier contains invalid characters";
        case PackageError::VersionOverflow:
            return "Version number exceeds maximum allowed value";

        // Constraint errors
        case PackageError::InvalidConstraint:
            return "Version constraint is not valid";
        case PackageError::EmptyConstraint:
            return "Version constraint is empty";
        case PackageError::UnsupportedOperator:
            return "Version constraint operator is not supported";
        case PackageError::IncompatibleConstraints:
            return "Version constraints cannot be satisfied together";

        // Manifest/package errors
        case PackageError::ManifestNotFound:
            return "Package manifest (dotpkg.json) not found";
        case PackageError::InvalidManifest:
            return "Package manifest contains invalid JSON";
        case PackageError::MissingPackageName:
            return "Package manifest is missing required 'name' field";
        case PackageError::MissingPackageVersion:
            return "Package manifest is missing required 'version' field";
        case PackageError::InvalidPackageName:
            return "Package name contains invalid characters";
        case PackageError::PackageNotFound:
            return "Package not found";
        case PackageError::PackageAlreadyExists:
            return "Package is already installed";

        // Lock file errors
        case PackageError::LockFileNotFound:
            return "Lock file (dotpkg.lock) not found";
        case PackageError::InvalidLockFile:
            return "Lock file is corrupted or has invalid format";
        case PackageError::LockFileMismatch:
            return "Lock file does not match manifest dependencies";
        case PackageError::LockFileVersionMismatch:
            return "Lock file format version is not supported";
        case PackageError::ChecksumMismatch:
            return "Package checksum does not match lock file";

        // Cache errors
        case PackageError::CacheNotFound:
            return "Package cache directory not found";
        case PackageError::CacheCorrupted:
            return "Package cache is corrupted";
        case PackageError::CacheWriteError:
            return "Failed to write to package cache";
        case PackageError::CacheReadError:
            return "Failed to read from package cache";
        case PackageError::CacheCleanupError:
            return "Failed to clean up package cache";

        // Registry errors
        case PackageError::RegistryNotFound:
            return "Package registry not found";
        case PackageError::RegistryCorrupted:
            return "Package registry is corrupted";
        case PackageError::RegistryWriteError:
            return "Failed to write to package registry";
        case PackageError::RegistryReadError:
            return "Failed to read from package registry";

        // Dependency resolution errors
        case PackageError::DependencyCycle:
            return "Circular dependency detected in package graph";
        case PackageError::UnresolvableDependency:
            return "Cannot find a version that satisfies all constraints";
        case PackageError::ConflictingVersions:
            return "Multiple incompatible versions required for the same package";
        case PackageError::MaxDepthExceeded:
            return "Dependency tree exceeds maximum allowed depth";
        case PackageError::ResolutionTimeout:
            return "Dependency resolution exceeded time limit";

        // I/O errors
        case PackageError::FileNotFound:
            return "File not found";
        case PackageError::FileReadError:
            return "Failed to read file";
        case PackageError::FileWriteError:
            return "Failed to write file";
        case PackageError::DirectoryCreateError:
            return "Failed to create directory";
        case PackageError::DirectoryNotFound:
            return "Directory not found";
        case PackageError::PermissionDenied:
            return "Permission denied";
        case PackageError::PathTooLong:
            return "Path exceeds system limits";

        // Archive errors
        case PackageError::InvalidArchive:
            return "Archive format is invalid or not supported";
        case PackageError::ArchiveExtractError:
            return "Failed to extract archive";
        case PackageError::ArchiveCreateError:
            return "Failed to create archive";
    }
    return "Unknown error";
}

}  // namespace dotvm::pkg

// ============================================================================
// std::formatter specialization
// ============================================================================

template <>
struct std::formatter<dotvm::pkg::PackageError> : std::formatter<std::string_view> {
    auto format(dotvm::pkg::PackageError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
