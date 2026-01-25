#pragma once

/// @file write_ahead_log.hpp
/// @brief STATE-007 Write-Ahead Log for crash recovery
///
/// WriteAheadLog provides durability guarantees by persisting all state
/// mutations to disk before applying them to the backend.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/log_record.hpp"
#include "dotvm/core/state/wal_error.hpp"

namespace dotvm::core::state {

// ============================================================================
// WAL Configuration
// ============================================================================

/// @brief Sync policy for the WAL
enum class WalSyncPolicy : std::uint8_t {
    None = 0,           ///< Never sync (fastest, least durable)
    Manual = 0,         ///< Alias for None - caller controls sync manually
    EveryCommit = 1,    ///< Sync after every transaction commit
    EveryNRecords = 2,  ///< Sync after N records
    Periodic = 3,       ///< Sync at regular intervals
};

/// @brief Configuration for WriteAheadLog
struct WalConfig {
    std::filesystem::path wal_directory{"/tmp/dotvm_wal"};  ///< WAL file directory
    std::size_t segment_size{64 * 1024 * 1024};             ///< Segment file size (64MB)
    std::size_t buffer_size{4096};                          ///< Write buffer size (4KB)
    WalSyncPolicy sync_policy{WalSyncPolicy::EveryCommit};  ///< Sync policy
    std::size_t sync_every_n_records{100};                  ///< For EveryNRecords policy
    std::chrono::milliseconds sync_interval{100};           ///< For Periodic policy

    /// @brief Create default configuration
    [[nodiscard]] static WalConfig defaults() noexcept { return WalConfig{}; }

    /// @brief Validate configuration
    [[nodiscard]] bool is_valid() const noexcept {
        return !wal_directory.empty() && segment_size > 0 && buffer_size > 0;
    }
};

// ============================================================================
// Checkpoint Info
// ============================================================================

/// @brief Information about a checkpoint
struct CheckpointInfo {
    LSN checkpoint_lsn;                               ///< LSN at checkpoint time
    std::chrono::system_clock::time_point timestamp;  ///< When checkpoint was created
    std::size_t records_checkpointed{0};              ///< Number of records included
};

// ============================================================================
// WriteAheadLog
// ============================================================================

/// @brief Write-Ahead Log for crash recovery and durability
///
/// All state mutations are first written to the WAL before being applied
/// to the actual backend. This enables recovery after crashes.
///
/// Thread Safety: Thread-safe. Uses atomic LSN generation and mutex for I/O.
///
/// @par Design Decisions
/// - Single active segment file for simplicity (no rotation for MVP)
/// - Buffer writes to reduce syscall overhead
/// - CRC32 checksums on each record for corruption detection
/// - Recovery replays all records from the WAL
class WriteAheadLog {
public:
    /// @brief Create a new WriteAheadLog
    ///
    /// Creates the WAL directory if it doesn't exist.
    ///
    /// @param config WAL configuration
    /// @return The created WAL, or error
    [[nodiscard]] static ::dotvm::core::Result<std::unique_ptr<WriteAheadLog>, WalError>
    create(WalConfig config);

    /// @brief Open an existing WriteAheadLog
    ///
    /// @param wal_dir Path to WAL directory
    /// @return The opened WAL, or error
    [[nodiscard]] static ::dotvm::core::Result<std::unique_ptr<WriteAheadLog>, WalError>
    open(const std::filesystem::path& wal_dir);

    ~WriteAheadLog();

    // Non-copyable, non-movable
    WriteAheadLog(const WriteAheadLog&) = delete;
    WriteAheadLog& operator=(const WriteAheadLog&) = delete;
    WriteAheadLog(WriteAheadLog&&) = delete;
    WriteAheadLog& operator=(WriteAheadLog&&) = delete;

    // ========================================================================
    // Core Operations
    // ========================================================================

    /// @brief Append a record to the WAL
    ///
    /// @param type Record type
    /// @param key Key data (empty for tx markers)
    /// @param value Value data (empty for delete/tx markers)
    /// @param tx_id Transaction ID
    /// @return The assigned LSN, or error
    [[nodiscard]] ::dotvm::core::Result<LSN, WalError> append(LogRecordType type,
                                                              std::span<const std::byte> key,
                                                              std::span<const std::byte> value,
                                                              TxId tx_id);

    /// @brief Sync buffered writes to disk
    ///
    /// Forces all buffered data to be written and fsynced.
    ///
    /// @return Success or error
    [[nodiscard]] ::dotvm::core::Result<void, WalError> sync();

    /// @brief Recover records from the WAL
    ///
    /// Reads all valid records from the WAL for replay.
    ///
    /// @return Vector of recovered records, or error
    [[nodiscard]] ::dotvm::core::Result<std::vector<LogRecord>, WalError> recover();

    /// @brief Create a checkpoint
    ///
    /// Records the current LSN as a checkpoint marker.
    ///
    /// @return Checkpoint information, or error
    [[nodiscard]] ::dotvm::core::Result<CheckpointInfo, WalError> checkpoint();

    /// @brief Truncate WAL before a given LSN
    ///
    /// Removes records with LSN < the given value. Used after checkpoint.
    ///
    /// @param lsn LSN threshold (records before this are removed)
    /// @return Success or error
    [[nodiscard]] ::dotvm::core::Result<void, WalError> truncate_before(LSN lsn);

    // ========================================================================
    // Accessors
    // ========================================================================

    /// @brief Get the current (next to be assigned) LSN
    [[nodiscard]] LSN current_lsn() const noexcept;

    /// @brief Get the last synced LSN
    [[nodiscard]] LSN last_synced_lsn() const noexcept;

    /// @brief Get the configuration
    [[nodiscard]] const WalConfig& config() const noexcept { return config_; }

private:
    explicit WriteAheadLog(WalConfig config);

    /// @brief Flush the write buffer to disk (without fsync)
    [[nodiscard]] ::dotvm::core::Result<void, WalError> flush_buffer();

    /// @brief Ensure the segment file is open
    [[nodiscard]] ::dotvm::core::Result<void, WalError> ensure_open();

    WalConfig config_;
    std::filesystem::path segment_path_;  ///< Current segment file path

    alignas(64) std::atomic<std::uint64_t> next_lsn_{1};    ///< Next LSN to assign
    alignas(64) std::atomic<std::uint64_t> synced_lsn_{0};  ///< Last synced LSN

    mutable std::shared_mutex io_mutex_;   ///< Mutex for file I/O
    std::vector<std::byte> write_buffer_;  ///< Write buffer
    int fd_{-1};                           ///< File descriptor (-1 if not open)
    std::size_t records_since_sync_{0};    ///< Records written since last sync
};

}  // namespace dotvm::core::state
