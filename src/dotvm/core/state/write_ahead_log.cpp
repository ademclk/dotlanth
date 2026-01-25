/// @file write_ahead_log.cpp
/// @brief STATE-007 Write-Ahead Log implementation
///
/// Implements durability through log-structured file I/O with buffering.

#include "dotvm/core/state/write_ahead_log.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace dotvm::core::state {

namespace {

/// @brief WAL segment file name
constexpr const char* SEGMENT_FILENAME = "wal_segment.log";

}  // namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

WriteAheadLog::WriteAheadLog(WalConfig config)
    : config_{std::move(config)},
      segment_path_{config_.wal_directory / SEGMENT_FILENAME} {
    write_buffer_.reserve(config_.buffer_size);
}

WriteAheadLog::~WriteAheadLog() {
    if (fd_ >= 0) {
        // Best-effort flush and close
        (void)flush_buffer();
        ::close(fd_);
        fd_ = -1;
    }
}

// ============================================================================
// Factory Methods
// ============================================================================

::dotvm::core::Result<std::unique_ptr<WriteAheadLog>, WalError>
WriteAheadLog::create(WalConfig config) {
    if (!config.is_valid()) {
        return WalError::WalWriteFailed;
    }

    // Create directory if needed
    std::error_code ec;
    std::filesystem::create_directories(config.wal_directory, ec);
    if (ec) {
        return WalError::WalWriteFailed;
    }

    auto wal = std::unique_ptr<WriteAheadLog>(new WriteAheadLog(std::move(config)));

    auto open_result = wal->ensure_open();
    if (open_result.is_err()) {
        return open_result.error();
    }

    return wal;
}

::dotvm::core::Result<std::unique_ptr<WriteAheadLog>, WalError>
WriteAheadLog::open(const std::filesystem::path& wal_dir) {
    if (!std::filesystem::exists(wal_dir)) {
        return WalError::WalReadFailed;
    }

    WalConfig config;
    config.wal_directory = wal_dir;

    auto wal = std::unique_ptr<WriteAheadLog>(new WriteAheadLog(std::move(config)));

    auto open_result = wal->ensure_open();
    if (open_result.is_err()) {
        return open_result.error();
    }

    return wal;
}

// ============================================================================
// Core Operations
// ============================================================================

::dotvm::core::Result<LSN, WalError> WriteAheadLog::append(
    LogRecordType type,
    std::span<const std::byte> key,
    std::span<const std::byte> value,
    TxId tx_id) {

    // Allocate LSN atomically
    std::uint64_t lsn_value = next_lsn_.fetch_add(1, std::memory_order_acq_rel);
    LSN lsn{lsn_value};

    // Create log record based on type
    LogRecord record;
    std::vector<std::byte> key_vec(key.begin(), key.end());
    std::vector<std::byte> value_vec(value.begin(), value.end());

    switch (type) {
        case LogRecordType::Put:
            record = LogRecord::create_put(lsn, std::move(key_vec), std::move(value_vec), tx_id);
            break;
        case LogRecordType::Delete:
            record = LogRecord::create_delete(lsn, std::move(key_vec), tx_id);
            break;
        case LogRecordType::TxBegin:
            record = LogRecord::create_tx_begin(lsn, tx_id);
            break;
        case LogRecordType::TxCommit:
            record = LogRecord::create_tx_commit(lsn, tx_id);
            break;
        case LogRecordType::TxAbort:
            record = LogRecord::create_tx_abort(lsn, tx_id);
            break;
        case LogRecordType::Checkpoint:
            // For checkpoint, value should contain the checkpoint LSN
            record = LogRecord::create_checkpoint(lsn, LSN{0});
            break;
    }

    std::vector<std::byte> serialized = record.serialize();

    // Write to buffer under lock
    {
        std::unique_lock lock(io_mutex_);

        // If record is larger than buffer, flush first
        if (serialized.size() > config_.buffer_size - write_buffer_.size()) {
            auto flush_result = flush_buffer();
            if (flush_result.is_err()) {
                return flush_result.error();
            }
        }

        // If still too large, write directly
        if (serialized.size() > config_.buffer_size) {
            ssize_t written = ::write(fd_, serialized.data(), serialized.size());
            if (written < 0 || static_cast<std::size_t>(written) != serialized.size()) {
                return WalError::WalWriteFailed;
            }
        } else {
            // Buffer the write
            write_buffer_.insert(write_buffer_.end(), serialized.begin(), serialized.end());
        }

        ++records_since_sync_;
    }

    // Auto-sync based on policy
    if (config_.sync_policy == WalSyncPolicy::EveryCommit &&
        (type == LogRecordType::TxCommit || type == LogRecordType::TxAbort)) {
        auto sync_result = sync();
        if (sync_result.is_err()) {
            return sync_result.error();
        }
    } else if (config_.sync_policy == WalSyncPolicy::EveryNRecords &&
               records_since_sync_ >= config_.sync_every_n_records) {
        auto sync_result = sync();
        if (sync_result.is_err()) {
            return sync_result.error();
        }
    }

    return lsn;
}

::dotvm::core::Result<void, WalError> WriteAheadLog::sync() {
    std::unique_lock lock(io_mutex_);

    // Flush buffer first
    auto flush_result = flush_buffer();
    if (flush_result.is_err()) {
        return flush_result.error();
    }

    // fsync the file
    if (fd_ >= 0) {
#ifdef __linux__
        if (::fdatasync(fd_) != 0) {
            return WalError::WalSyncFailed;
        }
#else
        if (::fsync(fd_) != 0) {
            return WalError::WalSyncFailed;
        }
#endif
    }

    // Update synced LSN
    synced_lsn_.store(next_lsn_.load(std::memory_order_acquire) - 1, std::memory_order_release);
    records_since_sync_ = 0;

    return {};
}

::dotvm::core::Result<std::vector<LogRecord>, WalError> WriteAheadLog::recover() {
    std::unique_lock lock(io_mutex_);

    // Flush any buffered data first
    auto flush_result = flush_buffer();
    if (flush_result.is_err()) {
        // Continue anyway - we want to recover what we can
    }

    // Read the entire file
    if (fd_ < 0) {
        return std::vector<LogRecord>{};
    }

    // Get file size
    struct stat st;
    if (::fstat(fd_, &st) != 0) {
        return WalError::WalReadFailed;
    }

    if (st.st_size == 0) {
        return std::vector<LogRecord>{};
    }

    // Seek to beginning
    if (::lseek(fd_, 0, SEEK_SET) == -1) {
        return WalError::WalReadFailed;
    }

    // Read all data
    std::vector<std::byte> file_data(static_cast<std::size_t>(st.st_size));
    ssize_t bytes_read = ::read(fd_, file_data.data(), file_data.size());
    if (bytes_read < 0) {
        return WalError::WalReadFailed;
    }
    file_data.resize(static_cast<std::size_t>(bytes_read));

    // Seek back to end for future appends
    ::lseek(fd_, 0, SEEK_END);

    // Parse records
    std::vector<LogRecord> records;
    std::size_t offset = 0;
    std::uint64_t max_lsn = 0;

    while (offset < file_data.size()) {
        // Need at least header + TxId + checksum
        if (offset + LogRecord::HEADER_SIZE + LogRecord::TXID_SERIALIZED_SIZE + sizeof(std::uint32_t) > file_data.size()) {
            break;  // Partial record at end
        }

        // Try to deserialize
        std::span<const std::byte> remaining(file_data.data() + offset, file_data.size() - offset);
        auto result = LogRecord::deserialize(remaining);

        if (result.is_err()) {
            // Corrupted record - stop recovery here
            break;
        }

        LogRecord& record = result.value();
        records.push_back(std::move(record));

        if (records.back().lsn.value > max_lsn) {
            max_lsn = records.back().lsn.value;
        }

        offset += records.back().serialized_size();
    }

    // Update next LSN to continue from recovered state
    if (max_lsn > 0) {
        next_lsn_.store(max_lsn + 1, std::memory_order_release);
        synced_lsn_.store(max_lsn, std::memory_order_release);
    }

    return records;
}

::dotvm::core::Result<CheckpointInfo, WalError> WriteAheadLog::checkpoint() {
    // Sync first to ensure all data is on disk
    auto sync_result = sync();
    if (sync_result.is_err()) {
        return sync_result.error();
    }

    LSN checkpoint_lsn{synced_lsn_.load(std::memory_order_acquire)};

    // Write checkpoint record
    auto lsn_result = append(
        LogRecordType::Checkpoint,
        {},
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(&checkpoint_lsn.value),
            sizeof(checkpoint_lsn.value)),
        TxId{});

    if (lsn_result.is_err()) {
        return lsn_result.error();
    }

    (void)sync();  // Best effort sync after checkpoint record

    CheckpointInfo info;
    info.checkpoint_lsn = checkpoint_lsn;
    info.timestamp = std::chrono::system_clock::now();
    info.records_checkpointed = static_cast<std::size_t>(checkpoint_lsn.value);

    return info;
}

::dotvm::core::Result<void, WalError> WriteAheadLog::truncate_before(LSN lsn) {
    // Sync first
    auto sync_result = sync();
    if (sync_result.is_err()) {
        return sync_result.error();
    }

    // Read all records (before acquiring lock for write operations)
    auto recover_result = recover();
    if (recover_result.is_err()) {
        return recover_result.error();
    }

    auto& records = recover_result.value();

    std::unique_lock lock(io_mutex_);

    // Filter records >= lsn
    std::vector<LogRecord> kept_records;
    for (auto& record : records) {
        if (record.lsn >= lsn) {
            kept_records.push_back(std::move(record));
        }
    }

    // Truncate file and rewrite kept records
    if (::ftruncate(fd_, 0) != 0) {
        return WalError::WalTruncateFailed;
    }
    ::lseek(fd_, 0, SEEK_SET);

    for (const auto& record : kept_records) {
        std::vector<std::byte> serialized = record.serialize();
        ssize_t written = ::write(fd_, serialized.data(), serialized.size());
        if (written < 0 || static_cast<std::size_t>(written) != serialized.size()) {
            return WalError::WalWriteFailed;
        }
    }

#ifdef __linux__
    ::fdatasync(fd_);
#else
    ::fsync(fd_);
#endif

    return {};
}

// ============================================================================
// Accessors
// ============================================================================

LSN WriteAheadLog::current_lsn() const noexcept {
    return LSN{next_lsn_.load(std::memory_order_acquire)};
}

LSN WriteAheadLog::last_synced_lsn() const noexcept {
    return LSN{synced_lsn_.load(std::memory_order_acquire)};
}

// ============================================================================
// Private Methods
// ============================================================================

::dotvm::core::Result<void, WalError> WriteAheadLog::flush_buffer() {
    if (write_buffer_.empty()) {
        return {};
    }

    if (fd_ < 0) {
        auto open_result = ensure_open();
        if (open_result.is_err()) {
            return open_result.error();
        }
    }

    ssize_t written = ::write(fd_, write_buffer_.data(), write_buffer_.size());
    if (written < 0 || static_cast<std::size_t>(written) != write_buffer_.size()) {
        return WalError::WalWriteFailed;
    }

    write_buffer_.clear();
    return {};
}

::dotvm::core::Result<void, WalError> WriteAheadLog::ensure_open() {
    if (fd_ >= 0) {
        return {};
    }

    // Open or create the segment file
    fd_ = ::open(segment_path_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        return WalError::WalWriteFailed;
    }

    return {};
}

}  // namespace dotvm::core::state
