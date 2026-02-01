/// @file chaos_test.cpp
/// @brief Chaos/fault injection tests for replication resilience
///
/// Tests replication behavior under various failure conditions.
/// This file tests:
/// - TestCluster infrastructure functionality
/// - FaultInjector utilities for network simulation
/// - Single-node leader behavior
/// - Transport partition and message loss simulation
///
/// Note: Full multi-node Raft consensus tests require actual message
/// delivery infrastructure. These tests focus on the building blocks
/// that enable chaos testing.

#include "dotvm/core/state/replication/replication_manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Mock Implementations for Testing
// ============================================================================

/// @brief Mock DeltaSource for chaos testing
class MockDeltaSource : public DeltaSource {
public:
    [[nodiscard]] LSN current_lsn() const noexcept override {
        return current_lsn_.load();
    }

    [[nodiscard]] std::vector<LogRecord> read_entries(LSN from_lsn, std::size_t max_entries,
                                                       std::size_t max_bytes) const override {
        std::lock_guard lock(mtx_);
        std::vector<LogRecord> result;
        std::size_t bytes_read = 0;

        for (const auto& [lsn, record] : entries_) {
            if (lsn < from_lsn) {
                continue;
            }
            std::size_t record_size = record.key.size() + record.value.size() + sizeof(LogRecord);
            if (bytes_read + record_size > max_bytes && !result.empty()) {
                break;
            }
            result.push_back(record);
            bytes_read += record_size;
            if (result.size() >= max_entries) {
                break;
            }
        }
        return result;
    }

    [[nodiscard]] bool is_available(LSN lsn) const noexcept override {
        std::lock_guard lock(mtx_);
        return !entries_.empty() && lsn.value <= current_lsn_.load().value;
    }

    void add_entry(LogRecord record) {
        std::lock_guard lock(mtx_);
        auto lsn = record.lsn;
        entries_.emplace(lsn, std::move(record));
        if (lsn > current_lsn_.load()) {
            current_lsn_.store(lsn);
        }
    }

    void set_current_lsn(LSN lsn) { current_lsn_.store(lsn); }

    void clear() {
        std::lock_guard lock(mtx_);
        entries_.clear();
        current_lsn_.store(LSN{0});
    }

private:
    mutable std::mutex mtx_;
    std::map<LSN, LogRecord, std::less<>> entries_;
    std::atomic<LSN> current_lsn_{LSN{0}};
};

/// @brief Mock DeltaSink for chaos testing
class MockDeltaSink : public DeltaSink {
public:
    [[nodiscard]] LSN applied_lsn() const noexcept override {
        return applied_lsn_.load();
    }

    [[nodiscard]] Result<void> apply_batch(const std::vector<LogRecord>& records) override {
        if (fail_applies_.load()) {
            return ReplicationError::DeltaApplyFailed;
        }

        // Simulate slow processing
        if (delay_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        }

        std::lock_guard lock(mtx_);
        for (const auto& record : records) {
            applied_records_.push_back(record);
            if (record.lsn > applied_lsn_.load()) {
                applied_lsn_.store(record.lsn);
            }
        }
        batches_applied_++;
        return {};
    }

    [[nodiscard]] MptHash mpt_root() const override { return root_; }

    void set_mpt_root(MptHash root) {
        std::lock_guard lock(mtx_);
        root_ = root;
    }

    void set_fail_applies(bool fail) { fail_applies_.store(fail); }

    void set_apply_delay_ms(int delay) { delay_ms_ = delay; }

    std::size_t batches_applied() const {
        std::lock_guard lock(mtx_);
        return batches_applied_;
    }

    std::vector<LogRecord> applied_records() const {
        std::lock_guard lock(mtx_);
        return applied_records_;
    }

    void clear() {
        std::lock_guard lock(mtx_);
        applied_records_.clear();
        applied_lsn_.store(LSN{0});
        batches_applied_ = 0;
    }

private:
    mutable std::mutex mtx_;
    std::atomic<LSN> applied_lsn_{LSN{0}};
    MptHash root_{};
    std::vector<LogRecord> applied_records_;
    std::size_t batches_applied_{0};
    std::atomic<bool> fail_applies_{false};
    int delay_ms_{0};
};

/// @brief Mock SnapshotSource for chaos testing
class MockSnapshotSource : public SnapshotSource {
public:
    [[nodiscard]] LSN snapshot_lsn() const override { return lsn_; }

    [[nodiscard]] std::size_t total_size() const override { return data_.size(); }

    [[nodiscard]] Result<std::vector<std::byte>> read_chunk(std::size_t offset,
                                                             std::size_t size) const override {
        std::lock_guard lock(mtx_);
        if (offset >= data_.size()) {
            return std::vector<std::byte>{};
        }
        auto end = std::min(offset + size, data_.size());
        return std::vector<std::byte>(data_.begin() + static_cast<std::ptrdiff_t>(offset),
                                      data_.begin() + static_cast<std::ptrdiff_t>(end));
    }

    [[nodiscard]] MptHash mpt_root() const override { return root_; }

    void set_snapshot(LSN lsn, std::vector<std::byte> data, MptHash root = {}) {
        std::lock_guard lock(mtx_);
        lsn_ = lsn;
        data_ = std::move(data);
        root_ = root;
    }

private:
    mutable std::mutex mtx_;
    LSN lsn_{0};
    std::vector<std::byte> data_;
    MptHash root_{};
};

/// @brief Mock SnapshotSink for chaos testing
class MockSnapshotSink : public SnapshotSink {
public:
    [[nodiscard]] Result<void> begin_snapshot(LSN lsn, std::size_t size) override {
        std::lock_guard lock(mtx_);
        if (fail_operations_) {
            return ReplicationError::SnapshotTransferFailed;
        }
        current_lsn_ = lsn;
        expected_size_ = size;
        data_.clear();
        data_.resize(size);
        receiving_ = true;
        return {};
    }

    [[nodiscard]] Result<void> write_chunk(std::size_t offset,
                                            std::span<const std::byte> data) override {
        std::lock_guard lock(mtx_);
        if (fail_operations_) {
            return ReplicationError::SnapshotTransferFailed;
        }
        if (!receiving_) {
            return ReplicationError::InvalidMessage;
        }
        if (offset + data.size() > data_.size()) {
            return ReplicationError::InvalidMessage;
        }
        std::copy(data.begin(), data.end(), data_.begin() + static_cast<std::ptrdiff_t>(offset));
        return {};
    }

    [[nodiscard]] Result<void> finalize_snapshot() override {
        std::lock_guard lock(mtx_);
        if (!receiving_) {
            return ReplicationError::InvalidMessage;
        }
        receiving_ = false;
        finalized_ = true;
        return {};
    }

    void abort_snapshot() override {
        std::lock_guard lock(mtx_);
        receiving_ = false;
        data_.clear();
    }

    [[nodiscard]] MptHash mpt_root() const override { return root_; }

    void set_fail_operations(bool fail) { fail_operations_.store(fail); }

    bool is_finalized() const {
        std::lock_guard lock(mtx_);
        return finalized_;
    }

private:
    mutable std::mutex mtx_;
    LSN current_lsn_{0};
    std::size_t expected_size_{0};
    std::vector<std::byte> data_;
    bool receiving_{false};
    bool finalized_{false};
    MptHash root_{};
    std::atomic<bool> fail_operations_{false};
};

// ============================================================================
// TestNode - Single node in a test cluster
// ============================================================================

/// @brief Represents a single node in the test cluster
struct TestNode {
    NodeId id;
    std::unique_ptr<MockTransport> transport;
    std::unique_ptr<MockDeltaSource> delta_source;
    std::unique_ptr<MockDeltaSink> delta_sink;
    std::unique_ptr<MockSnapshotSource> snapshot_source;
    std::unique_ptr<MockSnapshotSink> snapshot_sink;
    std::unique_ptr<ReplicationManager> manager;
    bool running{false};
};

// ============================================================================
// Helper Functions
// ============================================================================

static NodeId make_node_id(std::uint8_t seed) {
    NodeId id;
    for (std::size_t i = 0; i < id.data.size(); ++i) {
        id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
    }
    return id;
}

static LogRecord make_log_record(LSN lsn) {
    LogRecord record;
    record.lsn = lsn;
    record.type = LogRecordType::Put;
    record.key = {std::byte{'k'}, std::byte{static_cast<std::uint8_t>(lsn.value)}};
    record.value = {std::byte{'v'}, std::byte{static_cast<std::uint8_t>(lsn.value)}};
    record.tx_id = TxId{1, 1};
    record.checksum = 0;
    return record;
}

// ============================================================================
// Single Node Tests - Verify leader behavior
// ============================================================================

class SingleNodeChaosTest : public ::testing::Test {
protected:
    void SetUp() override {
        local_id_ = make_node_id(1);
        delta_source_ = std::make_unique<MockDeltaSource>();
        delta_sink_ = std::make_unique<MockDeltaSink>();
        snapshot_source_ = std::make_unique<MockSnapshotSource>();
        snapshot_sink_ = std::make_unique<MockSnapshotSink>();
        transport_ = std::make_unique<MockTransport>();
        transport_->start(local_id_);

        auto config = ReplicationConfig::defaults(local_id_);
        auto result = ReplicationManager::create(config, *delta_source_, *delta_sink_,
                                                  *snapshot_source_, *snapshot_sink_, *transport_);
        ASSERT_TRUE(result.is_ok());
        manager_ = std::move(result.value());
    }

    void TearDown() override {
        if (manager_) {
            manager_->stop();
        }
    }

    NodeId local_id_;
    std::unique_ptr<MockDeltaSource> delta_source_;
    std::unique_ptr<MockDeltaSink> delta_sink_;
    std::unique_ptr<MockSnapshotSource> snapshot_source_;
    std::unique_ptr<MockSnapshotSink> snapshot_sink_;
    std::unique_ptr<MockTransport> transport_;
    std::unique_ptr<ReplicationManager> manager_;
};

TEST_F(SingleNodeChaosTest, SingleNode_BecomesLeaderImmediately) {
    EXPECT_FALSE(manager_->is_leader());

    auto result = manager_->start();
    ASSERT_TRUE(result.is_ok());

    // Single node becomes leader immediately
    EXPECT_TRUE(manager_->is_leader());
    EXPECT_EQ(manager_->current_leader(), local_id_);
}

TEST_F(SingleNodeChaosTest, SingleNode_CanReplicate) {
    auto result = manager_->start();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(manager_->is_leader());

    // Add entry
    delta_source_->add_entry(make_log_record(LSN{1}));

    // Replicate
    auto rep_result = manager_->replicate(LSN{1});
    EXPECT_TRUE(rep_result.is_ok());
}

TEST_F(SingleNodeChaosTest, SingleNode_StepDownAndRestart) {
    auto result = manager_->start();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(manager_->is_leader());

    // Step down
    auto step_result = manager_->step_down();
    EXPECT_TRUE(step_result.is_ok());
    EXPECT_FALSE(manager_->is_leader());

    // Tick to allow re-election (single node should become leader again)
    for (int i = 0; i < 100; ++i) {
        manager_->tick();
        if (manager_->is_leader()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    // In a single-node cluster, node should become leader again
    EXPECT_TRUE(manager_->is_leader());
}

TEST_F(SingleNodeChaosTest, SingleNode_ReplicateAfterStepDown) {
    auto result = manager_->start();
    ASSERT_TRUE(result.is_ok());

    // Step down
    (void)manager_->step_down();

    // Trying to replicate when not leader should fail
    delta_source_->add_entry(make_log_record(LSN{1}));
    auto rep_result = manager_->replicate(LSN{1});
    EXPECT_TRUE(rep_result.is_err());
    EXPECT_EQ(rep_result.error(), ReplicationError::NotLeader);
}

// ============================================================================
// Transport Fault Injection Tests
// ============================================================================

class TransportFaultTest : public ::testing::Test {
protected:
    void SetUp() override {
        id1_ = make_node_id(1);
        id2_ = make_node_id(2);

        transport1_ = std::make_unique<MockTransport>();
        transport2_ = std::make_unique<MockTransport>();

        transport1_->start(id1_);
        transport2_->start(id2_);

        // Link transports
        transport1_->link_to(*transport2_);

        // Connect
        ASSERT_TRUE(transport1_->connect(id2_, "").is_ok());
        ASSERT_TRUE(transport2_->connect(id1_, "").is_ok());
    }

    void TearDown() override {
        transport1_->stop(std::chrono::milliseconds{100});
        transport2_->stop(std::chrono::milliseconds{100});
    }

    static std::vector<std::byte> make_test_message() {
        return {std::byte{'t'}, std::byte{'e'}, std::byte{'s'}, std::byte{'t'}};
    }

    NodeId id1_;
    NodeId id2_;
    std::unique_ptr<MockTransport> transport1_;
    std::unique_ptr<MockTransport> transport2_;
};

TEST_F(TransportFaultTest, MessageDeliveryWorks) {
    std::atomic<bool> received{false};
    std::vector<std::byte> received_data;

    transport2_->set_message_callback(
        [&](const NodeId& from, StreamType /*stream*/, std::span<const std::byte> data) {
            EXPECT_EQ(from, id1_);
            received_data = std::vector<std::byte>(data.begin(), data.end());
            received.store(true);
        });

    auto msg = make_test_message();
    auto result = transport1_->send(id2_, StreamType::Delta, msg);
    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(received.load());
    EXPECT_EQ(received_data, msg);
}

TEST_F(TransportFaultTest, PartitionDropsMessages) {
    std::atomic<int> receive_count{0};

    transport2_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive_count.fetch_add(1);
        });

    // Send message - should work
    auto msg = make_test_message();
    auto result = transport1_->send(id2_, StreamType::Delta, msg);
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(receive_count.load(), 1);

    // Create partition
    transport1_->partition_from(id2_);

    // Send message - should be silently dropped
    result = transport1_->send(id2_, StreamType::Delta, msg);
    EXPECT_TRUE(result.is_ok());  // Send succeeds, but message is dropped
    EXPECT_EQ(receive_count.load(), 1);  // Count unchanged
}

TEST_F(TransportFaultTest, PartitionHealing) {
    std::atomic<int> receive_count{0};

    transport2_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive_count.fetch_add(1);
        });

    auto msg = make_test_message();

    // Create partition
    transport1_->partition_from(id2_);

    // Message dropped
    (void)transport1_->send(id2_, StreamType::Delta, msg);
    EXPECT_EQ(receive_count.load(), 0);

    // Heal partition
    transport1_->heal_partition(id2_);

    // Message should now be delivered
    (void)transport1_->send(id2_, StreamType::Delta, msg);
    EXPECT_EQ(receive_count.load(), 1);
}

TEST_F(TransportFaultTest, MessageDropRate) {
    std::atomic<int> receive_count{0};

    transport2_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive_count.fetch_add(1);
        });

    // Set 50% drop rate
    transport1_->set_drop_rate(0.5);

    auto msg = make_test_message();
    const int num_messages = 100;

    for (int i = 0; i < num_messages; ++i) {
        (void)transport1_->send(id2_, StreamType::Delta, msg);
    }

    // Should receive roughly 50% of messages (with some tolerance)
    int received = receive_count.load();
    EXPECT_GT(received, 20);   // At least 20% received
    EXPECT_LT(received, 80);   // At most 80% received
}

TEST_F(TransportFaultTest, BidirectionalPartition) {
    std::atomic<int> receive1{0};
    std::atomic<int> receive2{0};

    transport1_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive1.fetch_add(1);
        });

    transport2_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive2.fetch_add(1);
        });

    auto msg = make_test_message();

    // Bidirectional partition
    transport1_->partition_from(id2_);
    transport2_->partition_from(id1_);

    // Both directions should fail
    (void)transport1_->send(id2_, StreamType::Delta, msg);
    (void)transport2_->send(id1_, StreamType::Delta, msg);

    EXPECT_EQ(receive1.load(), 0);
    EXPECT_EQ(receive2.load(), 0);

    // Heal both directions
    transport1_->heal_partition(id2_);
    transport2_->heal_partition(id1_);

    // Both directions should work
    (void)transport1_->send(id2_, StreamType::Delta, msg);
    (void)transport2_->send(id1_, StreamType::Delta, msg);

    EXPECT_EQ(receive1.load(), 1);
    EXPECT_EQ(receive2.load(), 1);
}

TEST_F(TransportFaultTest, AsymmetricPartition) {
    // Note: MockTransport's partition_from(peer) blocks both:
    // - Sending TO that peer (checked in send())
    // - Receiving FROM that peer (checked in deliver_message())
    //
    // So partition_from creates a bidirectional block from ONE node's perspective.
    // True asymmetric partitioning would require modifying the transport impl.
    //
    // This test verifies the current behavior: when node1 partitions from node2,
    // neither 1->2 nor 2->1 works from node1's perspective.

    std::atomic<int> receive1{0};
    std::atomic<int> receive2{0};

    transport1_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive1.fetch_add(1);
        });

    transport2_->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive2.fetch_add(1);
        });

    auto msg = make_test_message();

    // When transport1 partitions from id2:
    // - transport1 cannot send to id2 (blocks outgoing)
    // - transport1 cannot receive from id2 (blocks incoming)
    // But transport2's send to id1 still succeeds on transport2's side,
    // because the check happens on the receiver (transport1), not the sender.
    // Since transport1 has id2 in its partition list, it drops the incoming message.
    transport1_->partition_from(id2_);

    (void)transport1_->send(id2_, StreamType::Delta, msg);  // Blocked on sender side
    (void)transport2_->send(id1_, StreamType::Delta, msg);  // Blocked on receiver side

    // Both directions blocked when one node partitions
    EXPECT_EQ(receive1.load(), 0);  // transport1 blocked receive from id2
    EXPECT_EQ(receive2.load(), 0);  // transport1 blocked send to id2
}

// ============================================================================
// Delta Sink Fault Tests
// ============================================================================

class DeltaSinkFaultTest : public ::testing::Test {
protected:
    void SetUp() override {
        delta_sink_ = std::make_unique<MockDeltaSink>();
    }

    std::unique_ptr<MockDeltaSink> delta_sink_;
};

TEST_F(DeltaSinkFaultTest, FailedApplyReturnsError) {
    delta_sink_->set_fail_applies(true);

    std::vector<LogRecord> records = {make_log_record(LSN{1})};
    auto result = delta_sink_->apply_batch(records);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::DeltaApplyFailed);
}

TEST_F(DeltaSinkFaultTest, SlowApplyDelays) {
    delta_sink_->set_apply_delay_ms(50);

    std::vector<LogRecord> records = {make_log_record(LSN{1})};

    auto start = std::chrono::steady_clock::now();
    auto result = delta_sink_->apply_batch(records);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(result.is_ok());
    EXPECT_GE(elapsed, std::chrono::milliseconds{40});  // Some tolerance
}

TEST_F(DeltaSinkFaultTest, RecoverFromFailure) {
    delta_sink_->set_fail_applies(true);

    std::vector<LogRecord> records = {make_log_record(LSN{1})};
    auto result1 = delta_sink_->apply_batch(records);
    EXPECT_TRUE(result1.is_err());

    // Recover
    delta_sink_->set_fail_applies(false);

    auto result2 = delta_sink_->apply_batch(records);
    EXPECT_TRUE(result2.is_ok());
}

// ============================================================================
// Snapshot Sink Fault Tests
// ============================================================================

class SnapshotSinkFaultTest : public ::testing::Test {
protected:
    void SetUp() override {
        snapshot_sink_ = std::make_unique<MockSnapshotSink>();
    }

    std::unique_ptr<MockSnapshotSink> snapshot_sink_;
};

TEST_F(SnapshotSinkFaultTest, FailedBeginSnapshotReturnsError) {
    snapshot_sink_->set_fail_operations(true);

    auto result = snapshot_sink_->begin_snapshot(LSN{1}, 1024);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::SnapshotTransferFailed);
}

TEST_F(SnapshotSinkFaultTest, FailedWriteChunkReturnsError) {
    auto begin_result = snapshot_sink_->begin_snapshot(LSN{1}, 1024);
    EXPECT_TRUE(begin_result.is_ok());

    snapshot_sink_->set_fail_operations(true);

    std::vector<std::byte> chunk(100, std::byte{0x42});
    auto write_result = snapshot_sink_->write_chunk(0, chunk);
    EXPECT_TRUE(write_result.is_err());
    EXPECT_EQ(write_result.error(), ReplicationError::SnapshotTransferFailed);
}

TEST_F(SnapshotSinkFaultTest, AbortSnapshotClearsState) {
    auto begin_result = snapshot_sink_->begin_snapshot(LSN{1}, 1024);
    EXPECT_TRUE(begin_result.is_ok());

    snapshot_sink_->abort_snapshot();

    // After abort, finalize should fail
    auto finalize_result = snapshot_sink_->finalize_snapshot();
    EXPECT_TRUE(finalize_result.is_err());
}

// ============================================================================
// Statistics Tracking Tests
// ============================================================================

class TransportStatisticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        id1_ = make_node_id(1);
        id2_ = make_node_id(2);

        transport1_ = std::make_unique<MockTransport>();
        transport2_ = std::make_unique<MockTransport>();

        transport1_->start(id1_);
        transport2_->start(id2_);

        transport1_->link_to(*transport2_);
        ASSERT_TRUE(transport1_->connect(id2_, "").is_ok());
    }

    NodeId id1_;
    NodeId id2_;
    std::unique_ptr<MockTransport> transport1_;
    std::unique_ptr<MockTransport> transport2_;
};

TEST_F(TransportStatisticsTest, MessageCountsTracked) {
    auto msg = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}};

    EXPECT_EQ(transport1_->messages_sent(), 0U);

    (void)transport1_->send(id2_, StreamType::Delta, msg);
    (void)transport1_->send(id2_, StreamType::Delta, msg);
    (void)transport1_->send(id2_, StreamType::Delta, msg);

    EXPECT_EQ(transport1_->messages_sent(), 3U);
    EXPECT_EQ(transport2_->messages_received(), 3U);
}

TEST_F(TransportStatisticsTest, DroppedMessagesNotCountedAsReceived) {
    auto msg = std::vector<std::byte>{std::byte{0x01}};

    transport1_->partition_from(id2_);

    (void)transport1_->send(id2_, StreamType::Delta, msg);
    (void)transport1_->send(id2_, StreamType::Delta, msg);

    EXPECT_EQ(transport1_->messages_sent(), 2U);
    EXPECT_EQ(transport2_->messages_received(), 0U);
}

// ============================================================================
// Multi-Transport Connection Tests
// ============================================================================

class MultiTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (int i = 0; i < 3; ++i) {
            ids_[i] = make_node_id(static_cast<std::uint8_t>(i + 1));
            transports_[i] = std::make_unique<MockTransport>();
            transports_[i]->start(ids_[i]);
        }

        // Link all transports
        transports_[0]->link_to(*transports_[1]);
        transports_[0]->link_to(*transports_[2]);
        transports_[1]->link_to(*transports_[2]);

        // Connect all
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                if (i != j) {
                    (void)transports_[i]->connect(ids_[j], "");
                }
            }
        }
    }

    NodeId ids_[3];
    std::unique_ptr<MockTransport> transports_[3];
};

TEST_F(MultiTransportTest, BroadcastReachesAllPeers) {
    std::atomic<int> receive_count{0};

    for (int i = 1; i < 3; ++i) {
        transports_[i]->set_message_callback(
            [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
                receive_count.fetch_add(1);
            });
    }

    auto msg = std::vector<std::byte>{std::byte{0x42}};
    auto sent = transports_[0]->broadcast(StreamType::Delta, msg);

    EXPECT_EQ(sent, 2U);  // Broadcast to 2 peers
    EXPECT_EQ(receive_count.load(), 2);
}

TEST_F(MultiTransportTest, PartitionIsolatesOneNode) {
    std::atomic<int> receive0{0};
    std::atomic<int> receive1{0};
    std::atomic<int> receive2{0};

    transports_[0]->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive0.fetch_add(1);
        });
    transports_[1]->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive1.fetch_add(1);
        });
    transports_[2]->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            receive2.fetch_add(1);
        });

    // Partition node 2 from both 0 and 1
    transports_[0]->partition_from(ids_[2]);
    transports_[1]->partition_from(ids_[2]);
    transports_[2]->partition_from(ids_[0]);
    transports_[2]->partition_from(ids_[1]);

    auto msg = std::vector<std::byte>{std::byte{0x42}};

    // Node 0 broadcasts - should reach node 1 only
    transports_[0]->broadcast(StreamType::Delta, msg);
    EXPECT_EQ(receive1.load(), 1);
    EXPECT_EQ(receive2.load(), 0);

    // Node 2 broadcasts - should reach nobody
    transports_[2]->broadcast(StreamType::Delta, msg);
    EXPECT_EQ(receive0.load(), 0);
    EXPECT_EQ(receive1.load(), 1);  // Still 1 from before
}

TEST_F(MultiTransportTest, MajorityPartitionScenario) {
    // Simulate a 2-1 partition (majority vs minority)
    // Nodes 0, 1 can communicate, node 2 is isolated

    std::atomic<int> messages_in_majority{0};
    std::atomic<int> messages_to_minority{0};

    transports_[0]->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            messages_in_majority.fetch_add(1);
        });
    transports_[1]->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            messages_in_majority.fetch_add(1);
        });
    transports_[2]->set_message_callback(
        [&](const NodeId& /*from*/, StreamType /*stream*/, std::span<const std::byte> /*data*/) {
            messages_to_minority.fetch_add(1);
        });

    // Create partition: node 2 isolated
    transports_[0]->partition_from(ids_[2]);
    transports_[1]->partition_from(ids_[2]);
    transports_[2]->partition_from(ids_[0]);
    transports_[2]->partition_from(ids_[1]);

    auto msg = std::vector<std::byte>{std::byte{0x42}};

    // Communication within majority works
    (void)transports_[0]->send(ids_[1], StreamType::Raft, msg);
    (void)transports_[1]->send(ids_[0], StreamType::Raft, msg);

    EXPECT_EQ(messages_in_majority.load(), 2);
    EXPECT_EQ(messages_to_minority.load(), 0);
}

}  // namespace
}  // namespace dotvm::core::state::replication
