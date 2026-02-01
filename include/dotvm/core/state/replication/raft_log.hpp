#pragma once

/// @file raft_log.hpp
/// @brief STATE-006 Raft log storage
///
/// Provides persistent storage for Raft log entries (metadata operations).
/// This is separate from the WAL which stores state deltas.

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/raft_state.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Raft Log Configuration
// ============================================================================

/// @brief Configuration for Raft log storage
struct RaftLogConfig {
    std::string storage_path;            ///< Path to log storage file
    std::size_t max_entries{100000};     ///< Max entries before compaction
    std::size_t sync_batch_size{100};    ///< Entries to batch before fsync
    bool enable_compression{false};      ///< Compress entries on disk

    /// @brief Create default configuration
    [[nodiscard]] static RaftLogConfig defaults() noexcept { return RaftLogConfig{}; }

    /// @brief Create in-memory configuration (for testing)
    [[nodiscard]] static RaftLogConfig in_memory() noexcept {
        RaftLogConfig cfg;
        cfg.storage_path = ":memory:";
        return cfg;
    }
};

// ============================================================================
// Raft Log Interface
// ============================================================================

/// @brief Interface for Raft log storage
///
/// The Raft log stores cluster membership changes and configuration.
/// State data is replicated via WAL delta streaming, not the Raft log.
///
/// Thread Safety: Implementations MUST be thread-safe.
class RaftLog {
public:
    virtual ~RaftLog() = default;

    // Non-copyable, non-movable
    RaftLog(const RaftLog&) = delete;
    RaftLog& operator=(const RaftLog&) = delete;
    RaftLog(RaftLog&&) = delete;
    RaftLog& operator=(RaftLog&&) = delete;

protected:
    RaftLog() = default;

public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    // ========================================================================
    // Log Access
    // ========================================================================

    /// @brief Get number of entries in log
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    /// @brief Check if log is empty
    [[nodiscard]] virtual bool empty() const noexcept = 0;

    /// @brief Get first log index (after compaction, may not be 0)
    [[nodiscard]] virtual LogIndex first_index() const noexcept = 0;

    /// @brief Get last log index
    [[nodiscard]] virtual LogIndex last_index() const noexcept = 0;

    /// @brief Get term of entry at given index
    [[nodiscard]] virtual std::optional<Term> term_at(LogIndex index) const = 0;

    /// @brief Get entry at given index
    [[nodiscard]] virtual std::optional<RaftLogEntry> get(LogIndex index) const = 0;

    /// @brief Get entries in range [start, end)
    [[nodiscard]] virtual std::vector<RaftLogEntry> get_range(LogIndex start,
                                                               LogIndex end) const = 0;

    // ========================================================================
    // Log Modification
    // ========================================================================

    /// @brief Append entry to log
    ///
    /// @param entry Entry to append (index must be last_index + 1)
    /// @return Success or error
    [[nodiscard]] virtual Result<void> append(RaftLogEntry entry) = 0;

    /// @brief Append multiple entries to log
    ///
    /// @param entries Entries to append (must be contiguous starting at last_index + 1)
    /// @return Success or error
    [[nodiscard]] virtual Result<void> append_batch(std::vector<RaftLogEntry> entries) = 0;

    /// @brief Truncate log from given index (inclusive)
    ///
    /// Used when follower's log conflicts with leader's.
    /// Removes all entries with index >= from_index.
    ///
    /// @param from_index First index to remove
    /// @return Success or error
    [[nodiscard]] virtual Result<void> truncate(LogIndex from_index) = 0;

    // ========================================================================
    // Log Matching
    // ========================================================================

    /// @brief Check if log contains entry with given index and term
    ///
    /// Used by AppendEntries to verify log consistency.
    [[nodiscard]] virtual bool has_entry(LogIndex index, Term term) const = 0;

    /// @brief Find first index where terms differ (for conflict resolution)
    ///
    /// @param index Index to start checking
    /// @param term Expected term at index
    /// @return Index where conflict starts, or nullopt if no conflict
    [[nodiscard]] virtual std::optional<LogIndex> find_conflict(LogIndex index,
                                                                 Term term) const = 0;

    // ========================================================================
    // Compaction
    // ========================================================================

    /// @brief Compact log up to given index
    ///
    /// Removes entries with index < up_to_index.
    /// The entry at up_to_index becomes the new first entry.
    ///
    /// @param up_to_index Last index to keep
    /// @return Success or error
    [[nodiscard]] virtual Result<void> compact(LogIndex up_to_index) = 0;

    // ========================================================================
    // Persistence
    // ========================================================================

    /// @brief Sync all pending writes to disk
    [[nodiscard]] virtual Result<void> sync() = 0;
};

// ============================================================================
// In-Memory Raft Log
// ============================================================================

/// @brief In-memory implementation of RaftLog for testing
///
/// Stores entries in a vector. Not persistent, but fast and thread-safe.
class InMemoryRaftLog : public RaftLog {
public:
    /// @brief Create an empty in-memory log
    InMemoryRaftLog() = default;

    ~InMemoryRaftLog() override = default;

    // ========================================================================
    // RaftLog Interface
    // ========================================================================

    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] bool empty() const noexcept override;
    [[nodiscard]] LogIndex first_index() const noexcept override;
    [[nodiscard]] LogIndex last_index() const noexcept override;
    [[nodiscard]] std::optional<Term> term_at(LogIndex index) const override;
    [[nodiscard]] std::optional<RaftLogEntry> get(LogIndex index) const override;
    [[nodiscard]] std::vector<RaftLogEntry> get_range(LogIndex start, LogIndex end) const override;

    [[nodiscard]] Result<void> append(RaftLogEntry entry) override;
    [[nodiscard]] Result<void> append_batch(std::vector<RaftLogEntry> entries) override;
    [[nodiscard]] Result<void> truncate(LogIndex from_index) override;

    [[nodiscard]] bool has_entry(LogIndex index, Term term) const override;
    [[nodiscard]] std::optional<LogIndex> find_conflict(LogIndex index, Term term) const override;

    [[nodiscard]] Result<void> compact(LogIndex up_to_index) override;

    [[nodiscard]] Result<void> sync() override;

private:
    mutable std::mutex mtx_;
    std::vector<RaftLogEntry> entries_;
    LogIndex first_index_{1};  // Raft log indices start at 1
};

// ============================================================================
// Factory Function
// ============================================================================

/// @brief Create a Raft log with the given configuration
///
/// @param config Log configuration
/// @return Log instance or error
[[nodiscard]] Result<std::unique_ptr<RaftLog>, ReplicationError> create_raft_log(
    const RaftLogConfig& config);

}  // namespace dotvm::core::state::replication
