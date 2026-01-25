#pragma once

/// @file log_record.hpp
/// @brief STATE-007 Log record types for Write-Ahead Logging
///
/// Defines LSN, LogRecordType, and LogRecord for WAL operations.
/// Records are serialized to a binary format with CRC32 checksum.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/state_backend.hpp"  // For TxId
#include "dotvm/core/state/wal_error.hpp"

namespace dotvm::core::state {

// ============================================================================
// Log Sequence Number (LSN)
// ============================================================================

/// @brief Log Sequence Number - unique, monotonically increasing identifier for log records
///
/// LSNs are used to:
/// - Order log records
/// - Track recovery progress
/// - Identify checkpoint positions
struct LSN {
    std::uint64_t value{0};

    /// @brief Create an invalid LSN (value 0)
    [[nodiscard]] static constexpr LSN invalid() noexcept { return LSN{0}; }

    /// @brief Create the first valid LSN (value 1)
    [[nodiscard]] static constexpr LSN first() noexcept { return LSN{1}; }

    /// @brief Get the next LSN
    [[nodiscard]] constexpr LSN next() const noexcept { return LSN{value + 1}; }

    // Comparison operators
    [[nodiscard]] constexpr bool operator==(const LSN& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator!=(const LSN& other) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const LSN& other) const noexcept {
        return value < other.value;
    }
    [[nodiscard]] constexpr bool operator<=(const LSN& other) const noexcept {
        return value <= other.value;
    }
    [[nodiscard]] constexpr bool operator>(const LSN& other) const noexcept {
        return value > other.value;
    }
    [[nodiscard]] constexpr bool operator>=(const LSN& other) const noexcept {
        return value >= other.value;
    }
};

// ============================================================================
// Log Record Type
// ============================================================================

/// @brief Type of log record
enum class LogRecordType : std::uint8_t {
    Put = 0,         ///< Store a key-value pair
    Delete = 1,      ///< Delete a key
    TxBegin = 2,     ///< Transaction begin marker
    TxCommit = 3,    ///< Transaction commit marker
    TxAbort = 4,     ///< Transaction abort marker
    Checkpoint = 5,  ///< Checkpoint marker
};

/// @brief Convert log record type to string
[[nodiscard]] constexpr const char* to_string(LogRecordType type) noexcept {
    switch (type) {
        case LogRecordType::Put:
            return "Put";
        case LogRecordType::Delete:
            return "Delete";
        case LogRecordType::TxBegin:
            return "TxBegin";
        case LogRecordType::TxCommit:
            return "TxCommit";
        case LogRecordType::TxAbort:
            return "TxAbort";
        case LogRecordType::Checkpoint:
            return "Checkpoint";
    }
    return "Unknown";
}

// ============================================================================
// Log Record
// ============================================================================

/// @brief A single log record in the WAL
///
/// Binary format (little-endian):
/// ```
/// Header (16 bytes):
///   [0-7]   LSN         (uint64)
///   [8]     Type        (uint8)
///   [9]     Reserved    (uint8, 0x00)
///   [10-11] Key Length  (uint16)
///   [12-15] Value Length (uint32)
///
/// TxId (12 bytes):
///   [16-23] tx_id.id        (uint64)
///   [24-27] tx_id.generation (uint32)
///
/// Body (variable):
///   [28..] Key bytes
///   [..] Value bytes
///
/// Footer (4 bytes):
///   CRC32 of Header + TxId + Body
/// ```
struct LogRecord {
    /// @brief Header size in bytes (LSN + Type + Reserved + KeyLen + ValueLen)
    static constexpr std::size_t HEADER_SIZE = 16;

    LSN lsn;                       ///< Log sequence number
    LogRecordType type;            ///< Record type
    std::vector<std::byte> key;    ///< Key data (empty for tx markers)
    std::vector<std::byte> value;  ///< Value data (empty for delete/tx markers)
    TxId tx_id;                    ///< Transaction ID
    std::uint32_t checksum{0};     ///< CRC32 checksum

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /// @brief Create a Put record
    [[nodiscard]] static LogRecord create_put(LSN lsn, std::vector<std::byte> key,
                                              std::vector<std::byte> value, TxId tx_id);

    /// @brief Create a Delete record
    [[nodiscard]] static LogRecord create_delete(LSN lsn, std::vector<std::byte> key, TxId tx_id);

    /// @brief Create a TxBegin record
    [[nodiscard]] static LogRecord create_tx_begin(LSN lsn, TxId tx_id);

    /// @brief Create a TxCommit record
    [[nodiscard]] static LogRecord create_tx_commit(LSN lsn, TxId tx_id);

    /// @brief Create a TxAbort record
    [[nodiscard]] static LogRecord create_tx_abort(LSN lsn, TxId tx_id);

    /// @brief Create a Checkpoint record
    /// @param lsn The LSN of this checkpoint record
    /// @param checkpoint_lsn The LSN up to which data has been checkpointed
    [[nodiscard]] static LogRecord create_checkpoint(LSN lsn, LSN checkpoint_lsn);

    // ========================================================================
    // Serialization
    // ========================================================================

    /// @brief Serialize the record to bytes
    /// @return Serialized bytes including header, body, and CRC32 checksum
    [[nodiscard]] std::vector<std::byte> serialize() const;

    /// @brief Deserialize a record from bytes
    /// @param data Serialized record data
    /// @return Deserialized record, or WalError if invalid
    [[nodiscard]] static ::dotvm::core::Result<LogRecord, WalError>
    deserialize(std::span<const std::byte> data);

    // ========================================================================
    // Utilities
    // ========================================================================

    /// @brief Calculate CRC32 checksum for the record
    [[nodiscard]] std::uint32_t calculate_checksum() const;

    /// @brief Serialized size of TxId (without struct padding)
    static constexpr std::size_t TXID_SERIALIZED_SIZE = 12;  // uint64_t + uint32_t

    /// @brief Get the total serialized size
    [[nodiscard]] std::size_t serialized_size() const noexcept {
        return HEADER_SIZE + TXID_SERIALIZED_SIZE + key.size() + value.size() +
               sizeof(std::uint32_t);
    }
};

}  // namespace dotvm::core::state
