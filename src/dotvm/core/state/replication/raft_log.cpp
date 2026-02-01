/// @file raft_log.cpp
/// @brief Implementation of Raft log storage

#include "dotvm/core/state/replication/raft_log.hpp"

#include <algorithm>

namespace dotvm::core::state::replication {

// ============================================================================
// InMemoryRaftLog Implementation
// ============================================================================

std::size_t InMemoryRaftLog::size() const noexcept {
    std::lock_guard lock(mtx_);
    return entries_.size();
}

bool InMemoryRaftLog::empty() const noexcept {
    std::lock_guard lock(mtx_);
    return entries_.empty();
}

LogIndex InMemoryRaftLog::first_index() const noexcept {
    std::lock_guard lock(mtx_);
    return first_index_;
}

LogIndex InMemoryRaftLog::last_index() const noexcept {
    std::lock_guard lock(mtx_);
    if (entries_.empty()) {
        return first_index_.prev();  // No entries yet
    }
    return LogIndex{first_index_.value + entries_.size() - 1};
}

std::optional<Term> InMemoryRaftLog::term_at(LogIndex index) const {
    std::lock_guard lock(mtx_);

    if (entries_.empty()) {
        return std::nullopt;
    }

    if (index < first_index_ ||
        index.value >= first_index_.value + entries_.size()) {
        return std::nullopt;
    }

    auto array_index = static_cast<std::size_t>(index.value - first_index_.value);
    return entries_[array_index].term;
}

std::optional<RaftLogEntry> InMemoryRaftLog::get(LogIndex index) const {
    std::lock_guard lock(mtx_);

    if (entries_.empty()) {
        return std::nullopt;
    }

    if (index < first_index_ ||
        index.value >= first_index_.value + entries_.size()) {
        return std::nullopt;
    }

    auto array_index = static_cast<std::size_t>(index.value - first_index_.value);
    return entries_[array_index];
}

std::vector<RaftLogEntry> InMemoryRaftLog::get_range(LogIndex start, LogIndex end) const {
    std::lock_guard lock(mtx_);

    std::vector<RaftLogEntry> result;

    if (entries_.empty() || start >= end) {
        return result;
    }

    // Clamp to valid range
    auto effective_start_val = std::max(start.value, first_index_.value);
    auto effective_end_val = std::min(end.value, first_index_.value + entries_.size());

    if (effective_start_val >= effective_end_val) {
        return result;
    }

    auto start_idx = static_cast<std::size_t>(effective_start_val - first_index_.value);
    auto end_idx = static_cast<std::size_t>(effective_end_val - first_index_.value);

    result.reserve(end_idx - start_idx);
    for (auto i = start_idx; i < end_idx; ++i) {
        result.push_back(entries_[i]);
    }

    return result;
}

InMemoryRaftLog::Result<void> InMemoryRaftLog::append(RaftLogEntry entry) {
    std::lock_guard lock(mtx_);

    // Validate index
    LogIndex expected_index{first_index_.value + entries_.size()};
    if (entry.index != expected_index) {
        return ReplicationError::LogIndexGap;
    }

    entries_.push_back(std::move(entry));
    return {};
}

InMemoryRaftLog::Result<void> InMemoryRaftLog::append_batch(std::vector<RaftLogEntry> entries) {
    if (entries.empty()) {
        return {};
    }

    std::lock_guard lock(mtx_);

    // Validate first entry index
    LogIndex expected_index{first_index_.value + entries_.size()};
    if (entries[0].index != expected_index) {
        return ReplicationError::LogIndexGap;
    }

    // Validate contiguity
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].index.value != entries[i - 1].index.value + 1) {
            return ReplicationError::LogIndexGap;
        }
    }

    entries_.reserve(entries_.size() + entries.size());
    for (auto& entry : entries) {
        entries_.push_back(std::move(entry));
    }

    return {};
}

InMemoryRaftLog::Result<void> InMemoryRaftLog::truncate(LogIndex from_index) {
    std::lock_guard lock(mtx_);

    if (entries_.empty()) {
        return {};
    }

    if (from_index < first_index_) {
        // Truncate everything
        entries_.clear();
        return {};
    }

    auto array_index = static_cast<std::size_t>(from_index.value - first_index_.value);
    if (array_index < entries_.size()) {
        entries_.resize(array_index);
    }

    return {};
}

bool InMemoryRaftLog::has_entry(LogIndex index, Term term) const {
    auto actual_term = term_at(index);
    return actual_term.has_value() && actual_term.value() == term;
}

std::optional<LogIndex> InMemoryRaftLog::find_conflict(LogIndex index, Term term) const {
    std::lock_guard lock(mtx_);

    if (entries_.empty()) {
        return std::nullopt;
    }

    if (index < first_index_ ||
        index.value >= first_index_.value + entries_.size()) {
        return std::nullopt;
    }

    auto array_index = static_cast<std::size_t>(index.value - first_index_.value);
    if (entries_[array_index].term != term) {
        // Found conflict - return the index
        return index;
    }

    return std::nullopt;
}

InMemoryRaftLog::Result<void> InMemoryRaftLog::compact(LogIndex up_to_index) {
    std::lock_guard lock(mtx_);

    if (entries_.empty()) {
        return {};
    }

    if (up_to_index < first_index_) {
        // Nothing to compact
        return {};
    }

    auto array_index = static_cast<std::size_t>(up_to_index.value - first_index_.value);
    if (array_index >= entries_.size()) {
        // Can't compact past the end
        return ReplicationError::LogTruncated;
    }

    // Remove entries before up_to_index
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(array_index));
    first_index_ = up_to_index;

    return {};
}

InMemoryRaftLog::Result<void> InMemoryRaftLog::sync() {
    // In-memory log doesn't need to sync
    return {};
}

// ============================================================================
// Factory Function
// ============================================================================

Result<std::unique_ptr<RaftLog>, ReplicationError> create_raft_log(const RaftLogConfig& config) {
    if (config.storage_path == ":memory:" || config.storage_path.empty()) {
        std::unique_ptr<RaftLog> log = std::make_unique<InMemoryRaftLog>();
        return log;
    }

    // TODO: Implement persistent RaftLog backed by file storage
    // For now, return in-memory for any path
    std::unique_ptr<RaftLog> log = std::make_unique<InMemoryRaftLog>();
    return log;
}

}  // namespace dotvm::core::state::replication
