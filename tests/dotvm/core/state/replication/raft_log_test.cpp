/// @file raft_log_test.cpp
/// @brief Tests for Raft log storage

#include "dotvm/core/state/replication/raft_log.hpp"

#include <gtest/gtest.h>

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Test Helpers
// ============================================================================

class RaftLogTest : public ::testing::Test {
protected:
    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }

    static RaftLogEntry make_entry(Term term, LogIndex index) {
        RaftLogEntry entry;
        entry.term = term;
        entry.index = index;
        entry.command.type = RaftCommandType::Noop;
        return entry;
    }

    InMemoryRaftLog log_;
};

// ============================================================================
// Basic Operations Tests
// ============================================================================

TEST_F(RaftLogTest, EmptyLogState) {
    EXPECT_EQ(log_.size(), 0);
    EXPECT_TRUE(log_.empty());
    EXPECT_EQ(log_.first_index().value, 1);  // Raft indices start at 1
    EXPECT_EQ(log_.last_index().value, 0);   // No entries
}

TEST_F(RaftLogTest, AppendSingleEntry) {
    auto entry = make_entry(Term{1}, LogIndex{1});
    auto result = log_.append(std::move(entry));

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(log_.size(), 1);
    EXPECT_FALSE(log_.empty());
    EXPECT_EQ(log_.first_index().value, 1);
    EXPECT_EQ(log_.last_index().value, 1);
}

TEST_F(RaftLogTest, AppendMultipleEntries) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{3})).is_ok());

    EXPECT_EQ(log_.size(), 3);
    EXPECT_EQ(log_.last_index().value, 3);
}

TEST_F(RaftLogTest, AppendWithGapFails) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());

    // Try to append with a gap
    auto result = log_.append(make_entry(Term{1}, LogIndex{3}));

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::LogIndexGap);
}

TEST_F(RaftLogTest, AppendBatch) {
    std::vector<RaftLogEntry> entries = {
        make_entry(Term{1}, LogIndex{1}),
        make_entry(Term{1}, LogIndex{2}),
        make_entry(Term{2}, LogIndex{3}),
    };

    auto result = log_.append_batch(std::move(entries));

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(log_.size(), 3);
    EXPECT_EQ(log_.last_index().value, 3);
}

TEST_F(RaftLogTest, AppendBatchEmpty) {
    auto result = log_.append_batch({});
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(log_.empty());
}

TEST_F(RaftLogTest, AppendBatchWithGapFails) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());

    std::vector<RaftLogEntry> entries = {
        make_entry(Term{1}, LogIndex{3}),
        make_entry(Term{1}, LogIndex{4}),
    };

    auto result = log_.append_batch(std::move(entries));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::LogIndexGap);
}

TEST_F(RaftLogTest, AppendBatchNonContiguousFails) {
    std::vector<RaftLogEntry> entries = {
        make_entry(Term{1}, LogIndex{1}),
        make_entry(Term{1}, LogIndex{3}),  // Gap
    };

    auto result = log_.append_batch(std::move(entries));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::LogIndexGap);
}

// ============================================================================
// Retrieval Tests
// ============================================================================

TEST_F(RaftLogTest, GetEntry) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{3}, LogIndex{3})).is_ok());

    auto entry = log_.get(LogIndex{2});
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->term.value, 2);
    EXPECT_EQ(entry->index.value, 2);
}

TEST_F(RaftLogTest, GetEntryOutOfRange) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());

    EXPECT_FALSE(log_.get(LogIndex{0}).has_value());
    EXPECT_FALSE(log_.get(LogIndex{2}).has_value());
    EXPECT_FALSE(log_.get(LogIndex{100}).has_value());
}

TEST_F(RaftLogTest, TermAt) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{3})).is_ok());

    EXPECT_EQ(log_.term_at(LogIndex{1}).value().value, 1);
    EXPECT_EQ(log_.term_at(LogIndex{2}).value().value, 2);
    EXPECT_EQ(log_.term_at(LogIndex{3}).value().value, 2);
    EXPECT_FALSE(log_.term_at(LogIndex{4}).has_value());
}

TEST_F(RaftLogTest, GetRange) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{3})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{4})).is_ok());

    auto entries = log_.get_range(LogIndex{2}, LogIndex{4});
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0].index.value, 2);
    EXPECT_EQ(entries[1].index.value, 3);
}

TEST_F(RaftLogTest, GetRangeEmpty) {
    auto entries = log_.get_range(LogIndex{1}, LogIndex{5});
    EXPECT_TRUE(entries.empty());
}

TEST_F(RaftLogTest, GetRangeClampedToValid) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());

    // Request range extends past end
    auto entries = log_.get_range(LogIndex{1}, LogIndex{10});
    EXPECT_EQ(entries.size(), 2);
}

// ============================================================================
// Log Matching Tests
// ============================================================================

TEST_F(RaftLogTest, HasEntry) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{2})).is_ok());

    EXPECT_TRUE(log_.has_entry(LogIndex{1}, Term{1}));
    EXPECT_TRUE(log_.has_entry(LogIndex{2}, Term{2}));
    EXPECT_FALSE(log_.has_entry(LogIndex{1}, Term{2}));  // Wrong term
    EXPECT_FALSE(log_.has_entry(LogIndex{3}, Term{1}));  // Wrong index
}

TEST_F(RaftLogTest, FindConflict) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{3})).is_ok());

    // No conflict
    EXPECT_FALSE(log_.find_conflict(LogIndex{2}, Term{2}).has_value());
    EXPECT_FALSE(log_.find_conflict(LogIndex{3}, Term{2}).has_value());

    // Conflict
    auto conflict = log_.find_conflict(LogIndex{2}, Term{1});  // Entry 2 has term 2, not 1
    ASSERT_TRUE(conflict.has_value());
    EXPECT_EQ(conflict.value().value, 2);
}

// ============================================================================
// Truncation Tests
// ============================================================================

TEST_F(RaftLogTest, TruncateFromMiddle) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{3})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{4})).is_ok());

    auto result = log_.truncate(LogIndex{3});
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(log_.size(), 2);
    EXPECT_EQ(log_.last_index().value, 2);
    EXPECT_FALSE(log_.get(LogIndex{3}).has_value());
}

TEST_F(RaftLogTest, TruncateFromBeginning) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());

    auto result = log_.truncate(LogIndex{1});
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(log_.size(), 0);
    EXPECT_TRUE(log_.empty());
}

TEST_F(RaftLogTest, TruncatePastEnd) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());

    auto result = log_.truncate(LogIndex{10});  // Past end
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(log_.size(), 2);  // No change
}

TEST_F(RaftLogTest, TruncateEmptyLog) {
    auto result = log_.truncate(LogIndex{5});
    EXPECT_TRUE(result.is_ok());
}

// ============================================================================
// Compaction Tests
// ============================================================================

TEST_F(RaftLogTest, CompactLog) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{3})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{4})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{2}, LogIndex{5})).is_ok());

    auto result = log_.compact(LogIndex{3});
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(log_.first_index().value, 3);
    EXPECT_EQ(log_.last_index().value, 5);
    EXPECT_EQ(log_.size(), 3);

    // Old entries are gone
    EXPECT_FALSE(log_.get(LogIndex{1}).has_value());
    EXPECT_FALSE(log_.get(LogIndex{2}).has_value());

    // New entries still accessible
    auto entry = log_.get(LogIndex{3});
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->term.value, 2);
}

TEST_F(RaftLogTest, CompactPastEndFails) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{2})).is_ok());

    auto result = log_.compact(LogIndex{10});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::LogTruncated);
}

TEST_F(RaftLogTest, CompactEmptyLog) {
    auto result = log_.compact(LogIndex{5});
    EXPECT_TRUE(result.is_ok());
}

// ============================================================================
// Sync Tests
// ============================================================================

TEST_F(RaftLogTest, SyncSucceeds) {
    ASSERT_TRUE(log_.append(make_entry(Term{1}, LogIndex{1})).is_ok());
    auto result = log_.sync();
    EXPECT_TRUE(result.is_ok());
}

// ============================================================================
// Factory Tests
// ============================================================================

TEST(RaftLogFactoryTest, CreateInMemory) {
    auto result = create_raft_log(RaftLogConfig::in_memory());
    ASSERT_TRUE(result.is_ok());
    EXPECT_NE(result.value(), nullptr);
}

TEST(RaftLogFactoryTest, CreateWithEmptyPath) {
    RaftLogConfig config;
    config.storage_path = "";

    auto result = create_raft_log(config);
    ASSERT_TRUE(result.is_ok());
}

}  // namespace
}  // namespace dotvm::core::state::replication
