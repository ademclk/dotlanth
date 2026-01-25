#pragma once

/// @file wal_error.hpp
/// @brief STATE-007 WAL-specific error codes
///
/// Error codes for Write-Ahead Log operations, covering I/O errors,
/// checkpoint failures, and recovery issues.

#include <cstdint>

namespace dotvm::core::state {

// ============================================================================
// WAL Error Enum
// ============================================================================

/// @brief Error codes for Write-Ahead Log operations
///
/// Error codes are grouped by category in the 80-95 range:
/// - 80-87: I/O errors (write, read, sync, truncate)
/// - 88-91: Checkpoint errors
/// - 92-95: Recovery errors
enum class WalError : std::uint8_t {
    // I/O errors (80-87)
    WalWriteFailed = 80,     ///< Failed to write to WAL file
    WalReadFailed = 81,      ///< Failed to read from WAL file
    WalSyncFailed = 82,      ///< Failed to sync WAL to disk (fdatasync)
    WalCorrupted = 83,       ///< WAL file is corrupted (checksum mismatch)
    WalTruncateFailed = 84,  ///< Failed to truncate WAL file

    // Checkpoint errors (88-91)
    CheckpointFailed = 88,     ///< Checkpoint operation failed
    CheckpointCorrupted = 89,  ///< Checkpoint file is corrupted

    // Recovery errors (92-95)
    RecoveryFailed = 92,   ///< Recovery operation failed completely
    PartialRecovery = 93,  ///< Recovery succeeded partially (some records lost)
};

/// @brief Convert WAL error to human-readable string
[[nodiscard]] constexpr const char* to_string(WalError error) noexcept {
    switch (error) {
        case WalError::WalWriteFailed:
            return "WalWriteFailed";
        case WalError::WalReadFailed:
            return "WalReadFailed";
        case WalError::WalSyncFailed:
            return "WalSyncFailed";
        case WalError::WalCorrupted:
            return "WalCorrupted";
        case WalError::WalTruncateFailed:
            return "WalTruncateFailed";
        case WalError::CheckpointFailed:
            return "CheckpointFailed";
        case WalError::CheckpointCorrupted:
            return "CheckpointCorrupted";
        case WalError::RecoveryFailed:
            return "RecoveryFailed";
        case WalError::PartialRecovery:
            return "PartialRecovery";
    }
    return "Unknown";
}

/// @brief Check if a WAL error is recoverable
///
/// Recoverable errors indicate the operation can be retried or the system
/// can continue operating (possibly with some data loss).
///
/// @param error The error to check
/// @return true if the error is recoverable
[[nodiscard]] constexpr bool is_recoverable(WalError error) noexcept {
    switch (error) {
        case WalError::PartialRecovery:
            // Partial recovery means some records were recovered - system can continue
            return true;
        default:
            // All other WAL errors require manual intervention
            return false;
    }
}

}  // namespace dotvm::core::state
