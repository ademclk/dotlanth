/// @file message_serializer_test.cpp
/// @brief Tests for replication message serialization

#include "dotvm/core/state/replication/message_serializer.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class MessageSerializerTest : public ::testing::Test {
protected:
    // Helper to create a test NodeId
    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }

    // Helper to create random bytes
    static std::vector<std::byte> make_random_bytes(std::size_t size, unsigned seed = 42) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(0, 255);
        std::vector<std::byte> bytes(size);
        for (auto& b : bytes) {
            b = static_cast<std::byte>(dist(rng));
        }
        return bytes;
    }
};

// ============================================================================
// Error Code Tests
// ============================================================================

TEST(ReplicationErrorTest, ToStringCoversAllErrors) {
    // Transport errors
    EXPECT_EQ(to_string(ReplicationError::ConnectionFailed), "ConnectionFailed");
    EXPECT_EQ(to_string(ReplicationError::ConnectionClosed), "ConnectionClosed");
    EXPECT_EQ(to_string(ReplicationError::StreamError), "StreamError");
    EXPECT_EQ(to_string(ReplicationError::Timeout), "Timeout");
    EXPECT_EQ(to_string(ReplicationError::TlsError), "TlsError");
    EXPECT_EQ(to_string(ReplicationError::BackpressureExceeded), "BackpressureExceeded");

    // Consensus errors
    EXPECT_EQ(to_string(ReplicationError::NotLeader), "NotLeader");
    EXPECT_EQ(to_string(ReplicationError::NoLeader), "NoLeader");
    EXPECT_EQ(to_string(ReplicationError::ElectionTimeout), "ElectionTimeout");
    EXPECT_EQ(to_string(ReplicationError::QuorumNotReached), "QuorumNotReached");

    // Sync errors
    EXPECT_EQ(to_string(ReplicationError::DeltaApplyFailed), "DeltaApplyFailed");
    EXPECT_EQ(to_string(ReplicationError::SnapshotTransferFailed), "SnapshotTransferFailed");
    EXPECT_EQ(to_string(ReplicationError::VerificationFailed), "VerificationFailed");
}

TEST(ReplicationErrorTest, IsRecoverableClassifiesCorrectly) {
    // Recoverable errors
    EXPECT_TRUE(is_recoverable(ReplicationError::Timeout));
    EXPECT_TRUE(is_recoverable(ReplicationError::BackpressureExceeded));
    EXPECT_TRUE(is_recoverable(ReplicationError::NotLeader));
    EXPECT_TRUE(is_recoverable(ReplicationError::NoLeader));
    EXPECT_TRUE(is_recoverable(ReplicationError::ElectionTimeout));
    EXPECT_TRUE(is_recoverable(ReplicationError::LsnGap));
    EXPECT_TRUE(is_recoverable(ReplicationError::ConfigurationInProgress));

    // Non-recoverable errors
    EXPECT_FALSE(is_recoverable(ReplicationError::ConnectionFailed));
    EXPECT_FALSE(is_recoverable(ReplicationError::TlsError));
    EXPECT_FALSE(is_recoverable(ReplicationError::LogInconsistent));
    EXPECT_FALSE(is_recoverable(ReplicationError::VerificationFailed));
    EXPECT_FALSE(is_recoverable(ReplicationError::InvalidNodeId));
}

TEST(ReplicationErrorTest, CategoryClassifiers) {
    EXPECT_TRUE(is_transport_error(ReplicationError::ConnectionFailed));
    EXPECT_TRUE(is_transport_error(ReplicationError::Timeout));
    EXPECT_FALSE(is_transport_error(ReplicationError::NotLeader));

    EXPECT_TRUE(is_consensus_error(ReplicationError::NotLeader));
    EXPECT_TRUE(is_consensus_error(ReplicationError::QuorumNotReached));
    EXPECT_FALSE(is_consensus_error(ReplicationError::Timeout));

    EXPECT_TRUE(is_sync_error(ReplicationError::DeltaApplyFailed));
    EXPECT_TRUE(is_sync_error(ReplicationError::VerificationFailed));
    EXPECT_FALSE(is_sync_error(ReplicationError::NotLeader));

    EXPECT_TRUE(is_config_error(ReplicationError::InvalidNodeId));
    EXPECT_TRUE(is_config_error(ReplicationError::ClusterNotInitialized));
    EXPECT_FALSE(is_config_error(ReplicationError::Timeout));
}

// ============================================================================
// NodeId Tests
// ============================================================================

TEST(NodeIdTest, NullNodeId) {
    NodeId id = NodeId::null();
    EXPECT_TRUE(id.is_null());

    NodeId non_null;
    non_null.data[0] = std::uint8_t{1};
    EXPECT_FALSE(non_null.is_null());
}

TEST(NodeIdTest, Comparison) {
    NodeId a, b;
    a.data[0] = std::uint8_t{1};
    b.data[0] = std::uint8_t{2};

    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
    EXPECT_FALSE(a == b);

    NodeId c = a;
    EXPECT_TRUE(a == c);
}

TEST_F(MessageSerializerTest, NodeIdHexConversion) {
    NodeId id = make_node_id(0xAB);
    std::string hex = id.to_hex();
    EXPECT_EQ(hex.size(), 32);  // 16 bytes = 32 hex chars

    auto parsed = NodeId::from_hex(hex);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed.value(), id);
}

TEST(NodeIdTest, InvalidHexReturnsEmpty) {
    EXPECT_FALSE(NodeId::from_hex("not-valid-hex").has_value());
    EXPECT_FALSE(NodeId::from_hex("abc").has_value());  // Too short
    EXPECT_FALSE(NodeId::from_hex("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz").has_value());  // Invalid chars
}

// ============================================================================
// Term and LogIndex Tests
// ============================================================================

TEST(TermTest, Ordering) {
    Term t1{1}, t2{2};
    EXPECT_TRUE(t1 < t2);
    EXPECT_TRUE(t1 <= t2);
    EXPECT_FALSE(t1 > t2);
    EXPECT_FALSE(t1 >= t2);
    EXPECT_FALSE(t1 == t2);

    Term t3 = t1.next();
    EXPECT_EQ(t3.value, 2);
}

TEST(LogIndexTest, Validity) {
    EXPECT_FALSE(LogIndex::invalid().is_valid());
    EXPECT_TRUE(LogIndex::first().is_valid());

    LogIndex idx{5};
    EXPECT_TRUE(idx.is_valid());
    EXPECT_EQ(idx.next().value, 6);
    EXPECT_EQ(idx.prev().value, 4);

    // prev() on 0 stays at 0
    EXPECT_EQ(LogIndex{0}.prev().value, 0);
}

// ============================================================================
// Message Type Tests
// ============================================================================

TEST(MessageTypeTest, ToStringCoversAll) {
    EXPECT_EQ(to_string(MessageType::RequestVote), "RequestVote");
    EXPECT_EQ(to_string(MessageType::AppendEntries), "AppendEntries");
    EXPECT_EQ(to_string(MessageType::DeltaBatch), "DeltaBatch");
    EXPECT_EQ(to_string(MessageType::SnapshotChunk), "SnapshotChunk");
    EXPECT_EQ(to_string(MessageType::Heartbeat), "Heartbeat");
    EXPECT_EQ(to_string(MessageType::ClusterConfig), "ClusterConfig");
}

TEST(MessageTypeTest, GetMessageTypeFromVariant) {
    ReplicationMessage msg = RequestVote{};
    EXPECT_EQ(get_message_type(msg), MessageType::RequestVote);

    msg = DeltaBatch{};
    EXPECT_EQ(get_message_type(msg), MessageType::DeltaBatch);

    msg = Heartbeat{};
    EXPECT_EQ(get_message_type(msg), MessageType::Heartbeat);
}

// ============================================================================
// Serialization Round-Trip Tests
// ============================================================================

TEST_F(MessageSerializerTest, RequestVoteRoundTrip) {
    RequestVote original;
    original.term = Term{42};
    original.candidate_id = make_node_id(1);
    original.last_log_index = LogIndex{100};
    original.last_log_term = Term{40};

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok()) << "Serialization failed";

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok()) << "Deserialization failed";

    auto* result = std::get_if<RequestVote>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->term, original.term);
    EXPECT_EQ(result->candidate_id, original.candidate_id);
    EXPECT_EQ(result->last_log_index, original.last_log_index);
    EXPECT_EQ(result->last_log_term, original.last_log_term);
}

TEST_F(MessageSerializerTest, RequestVoteResponseRoundTrip) {
    RequestVoteResponse original;
    original.term = Term{42};
    original.vote_granted = true;

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<RequestVoteResponse>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->term, original.term);
    EXPECT_EQ(result->vote_granted, original.vote_granted);
}

TEST_F(MessageSerializerTest, AppendEntriesEmptyRoundTrip) {
    // Empty entries = heartbeat
    AppendEntries original;
    original.term = Term{10};
    original.leader_id = make_node_id(5);
    original.prev_log_index = LogIndex{50};
    original.prev_log_term = Term{9};
    original.leader_commit = LogIndex{45};

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<AppendEntries>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->term, original.term);
    EXPECT_EQ(result->leader_id, original.leader_id);
    EXPECT_EQ(result->prev_log_index, original.prev_log_index);
    EXPECT_EQ(result->prev_log_term, original.prev_log_term);
    EXPECT_EQ(result->leader_commit, original.leader_commit);
    EXPECT_TRUE(result->entries.empty());
}

TEST_F(MessageSerializerTest, AppendEntriesWithEntriesRoundTrip) {
    AppendEntries original;
    original.term = Term{10};
    original.leader_id = make_node_id(5);
    original.prev_log_index = LogIndex{50};
    original.prev_log_term = Term{9};
    original.leader_commit = LogIndex{45};

    // Add some entries
    RaftLogEntry entry1;
    entry1.term = Term{10};
    entry1.index = LogIndex{51};
    entry1.command.type = RaftCommandType::AddNode;
    entry1.command.data = {std::byte{0x01}, std::byte{0x02}};

    RaftLogEntry entry2;
    entry2.term = Term{10};
    entry2.index = LogIndex{52};
    entry2.command.type = RaftCommandType::Noop;

    original.entries.push_back(entry1);
    original.entries.push_back(entry2);

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<AppendEntries>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    ASSERT_EQ(result->entries.size(), 2);
    EXPECT_EQ(result->entries[0].term, entry1.term);
    EXPECT_EQ(result->entries[0].index, entry1.index);
    EXPECT_EQ(result->entries[0].command.type, entry1.command.type);
    EXPECT_EQ(result->entries[0].command.data, entry1.command.data);
    EXPECT_EQ(result->entries[1].command.type, RaftCommandType::Noop);
}

TEST_F(MessageSerializerTest, AppendEntriesResponseRoundTrip) {
    AppendEntriesResponse original;
    original.term = Term{10};
    original.success = true;
    original.match_index = LogIndex{52};

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<AppendEntriesResponse>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->term, original.term);
    EXPECT_EQ(result->success, original.success);
    EXPECT_EQ(result->match_index, original.match_index);
}

TEST_F(MessageSerializerTest, DeltaBatchRoundTrip) {
    DeltaBatch original;
    original.sender_id = make_node_id(3);
    original.start_lsn = LSN{100};
    original.end_lsn = LSN{102};

    // Add log records
    original.entries.push_back(
        LogRecord::create_put(LSN{100}, {std::byte{0x01}}, {std::byte{0x02}}, TxId{1, 0}));
    original.entries.push_back(LogRecord::create_delete(LSN{101}, {std::byte{0x03}}, TxId{1, 0}));
    original.entries.push_back(LogRecord::create_tx_commit(LSN{102}, TxId{1, 0}));

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<DeltaBatch>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->sender_id, original.sender_id);
    EXPECT_EQ(result->start_lsn, original.start_lsn);
    EXPECT_EQ(result->end_lsn, original.end_lsn);
    ASSERT_EQ(result->entries.size(), 3);
    EXPECT_EQ(result->entries[0].type, LogRecordType::Put);
    EXPECT_EQ(result->entries[1].type, LogRecordType::Delete);
    EXPECT_EQ(result->entries[2].type, LogRecordType::TxCommit);
}

TEST_F(MessageSerializerTest, DeltaAckRoundTrip) {
    DeltaAck original;
    original.node_id = make_node_id(4);
    original.acked_lsn = LSN{102};
    original.success = false;
    original.error_msg = "Apply failed: key too large";

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<DeltaAck>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->node_id, original.node_id);
    EXPECT_EQ(result->acked_lsn, original.acked_lsn);
    EXPECT_EQ(result->success, original.success);
    EXPECT_EQ(result->error_msg, original.error_msg);
}

TEST_F(MessageSerializerTest, SnapshotChunkRoundTrip) {
    SnapshotChunk original;
    original.sender_id = make_node_id(1);
    original.snapshot_lsn = LSN{1000};
    original.chunk_index = 5;
    original.total_chunks = 10;
    original.total_bytes = 640 * 1024;
    original.data = make_random_bytes(64 * 1024);
    original.is_last = false;

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<SnapshotChunk>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->sender_id, original.sender_id);
    EXPECT_EQ(result->snapshot_lsn, original.snapshot_lsn);
    EXPECT_EQ(result->chunk_index, original.chunk_index);
    EXPECT_EQ(result->total_chunks, original.total_chunks);
    EXPECT_EQ(result->total_bytes, original.total_bytes);
    EXPECT_EQ(result->data, original.data);
    EXPECT_EQ(result->is_last, original.is_last);
}

TEST_F(MessageSerializerTest, HeartbeatRoundTrip) {
    Heartbeat original;
    original.sender_id = make_node_id(1);
    original.timestamp_us = 1234567890123456ULL;
    original.current_lsn = LSN{500};
    original.is_leader = true;

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<Heartbeat>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->sender_id, original.sender_id);
    EXPECT_EQ(result->timestamp_us, original.timestamp_us);
    EXPECT_EQ(result->current_lsn, original.current_lsn);
    EXPECT_EQ(result->is_leader, original.is_leader);
}

TEST_F(MessageSerializerTest, ClusterConfigRoundTrip) {
    ClusterConfig original;
    original.config_index = 5;
    original.members = {make_node_id(1), make_node_id(2), make_node_id(3)};
    original.learners = {make_node_id(4)};

    auto serialized = MessageSerializer::serialize(original);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    auto* result = std::get_if<ClusterConfig>(&deserialized.value());
    ASSERT_NE(result, nullptr);

    EXPECT_EQ(result->config_index, original.config_index);
    EXPECT_EQ(result->members.size(), original.members.size());
    EXPECT_EQ(result->learners.size(), original.learners.size());

    for (std::size_t i = 0; i < original.members.size(); ++i) {
        EXPECT_EQ(result->members[i], original.members[i]);
    }
}

// ============================================================================
// Peek Tests
// ============================================================================

TEST_F(MessageSerializerTest, PeekType) {
    Heartbeat msg;
    msg.sender_id = make_node_id(1);

    auto serialized = MessageSerializer::serialize(msg);
    ASSERT_TRUE(serialized.is_ok());

    auto type_result = MessageSerializer::peek_type(serialized.value());
    ASSERT_TRUE(type_result.is_ok());
    EXPECT_EQ(type_result.value(), MessageType::Heartbeat);
}

TEST_F(MessageSerializerTest, PeekSize) {
    Heartbeat msg;
    msg.sender_id = make_node_id(1);

    auto serialized = MessageSerializer::serialize(msg);
    ASSERT_TRUE(serialized.is_ok());

    auto size_result = MessageSerializer::peek_size(serialized.value());
    ASSERT_TRUE(size_result.is_ok());
    EXPECT_EQ(size_result.value(), serialized.value().size());
}

TEST_F(MessageSerializerTest, PeekTypeTooSmall) {
    std::vector<std::byte> small_data(MESSAGE_HEADER_SIZE - 1);
    auto result = MessageSerializer::peek_type(small_data);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::InvalidMessage);
}

// ============================================================================
// Error Cases
// ============================================================================

TEST_F(MessageSerializerTest, DeserializeEmptyData) {
    auto result = MessageSerializer::deserialize({});
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::InvalidMessage);
}

TEST_F(MessageSerializerTest, DeserializeTruncatedData) {
    Heartbeat msg;
    msg.sender_id = make_node_id(1);

    auto serialized = MessageSerializer::serialize(msg);
    ASSERT_TRUE(serialized.is_ok());

    // Truncate the data
    auto truncated = serialized.value();
    truncated.resize(truncated.size() / 2);

    auto result = MessageSerializer::deserialize(truncated);
    EXPECT_TRUE(result.is_err());
}

TEST_F(MessageSerializerTest, DeserializeCorruptedChecksum) {
    Heartbeat msg;
    msg.sender_id = make_node_id(1);

    auto serialized = MessageSerializer::serialize(msg);
    ASSERT_TRUE(serialized.is_ok());

    // Corrupt the checksum (bytes 8-11 in header)
    auto corrupted = serialized.value();
    corrupted[8] = std::byte{0xFF};
    corrupted[9] = std::byte{0xFF};

    auto result = MessageSerializer::deserialize(corrupted);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::ChecksumMismatch);
}

TEST_F(MessageSerializerTest, DeserializeInvalidVersion) {
    Heartbeat msg;
    msg.sender_id = make_node_id(1);

    auto serialized = MessageSerializer::serialize(msg);
    ASSERT_TRUE(serialized.is_ok());

    // Corrupt the version byte (byte 1)
    auto corrupted = serialized.value();
    corrupted[1] = std::byte{0xFF};

    auto result = MessageSerializer::deserialize(corrupted);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::InvalidMessage);
}

// ============================================================================
// CRC32 Tests
// ============================================================================

TEST_F(MessageSerializerTest, CRC32Deterministic) {
    auto data = make_random_bytes(1000);
    auto crc1 = MessageSerializer::calculate_crc32(data);
    auto crc2 = MessageSerializer::calculate_crc32(data);
    EXPECT_EQ(crc1, crc2);
}

TEST_F(MessageSerializerTest, CRC32DifferentForDifferentData) {
    auto data1 = make_random_bytes(1000, 1);
    auto data2 = make_random_bytes(1000, 2);

    auto crc1 = MessageSerializer::calculate_crc32(data1);
    auto crc2 = MessageSerializer::calculate_crc32(data2);
    EXPECT_NE(crc1, crc2);
}

// ============================================================================
// Generic Variant Serialization
// ============================================================================

TEST_F(MessageSerializerTest, SerializeViaVariant) {
    ReplicationMessage msg = Heartbeat{make_node_id(1), 12345, LSN{100}, false};

    auto serialized = MessageSerializer::serialize(msg);
    ASSERT_TRUE(serialized.is_ok());

    auto deserialized = MessageSerializer::deserialize(serialized.value());
    ASSERT_TRUE(deserialized.is_ok());

    EXPECT_EQ(get_message_type(deserialized.value()), MessageType::Heartbeat);
}

}  // namespace
}  // namespace dotvm::core::state::replication
