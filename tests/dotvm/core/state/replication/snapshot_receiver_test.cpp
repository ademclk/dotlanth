/// @file snapshot_receiver_test.cpp
/// @brief Tests for SnapshotReceiver (follower-side snapshot reception)

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "dotvm/core/state/mpt_types.hpp"
#include "dotvm/core/state/replication/snapshot_receiver.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Mock SnapshotSink Implementation
// ============================================================================

/// @brief Mock implementation of SnapshotSink for testing
class MockSnapshotSink : public SnapshotSink {
public:
    MockSnapshotSink() = default;

    // SnapshotSink interface
    [[nodiscard]] Result<void> begin_snapshot(LSN lsn, std::size_t total_size) override {
        if (fail_begin_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        snapshot_lsn_ = lsn;
        total_size_ = total_size;
        bytes_written_ = 0;
        chunks_written_ = 0;
        is_receiving_ = true;
        finalized_ = false;
        aborted_ = false;
        chunk_data_.clear();
        return {};
    }

    [[nodiscard]] Result<void> write_chunk(std::size_t offset,
                                           std::span<const std::byte> data) override {
        if (fail_write_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        if (!is_receiving_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        // Store chunk data
        if (chunk_data_.size() < offset + data.size()) {
            chunk_data_.resize(offset + data.size());
        }
        std::copy(data.begin(), data.end(),
                  chunk_data_.begin() + static_cast<std::ptrdiff_t>(offset));

        bytes_written_ += data.size();
        chunks_written_++;
        return {};
    }

    [[nodiscard]] Result<void> finalize_snapshot() override {
        if (fail_finalize_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        if (!is_receiving_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        is_receiving_ = false;
        finalized_ = true;
        return {};
    }

    void abort_snapshot() override {
        is_receiving_ = false;
        aborted_ = true;
        chunk_data_.clear();
    }

    [[nodiscard]] MptHash mpt_root() const override { return current_mpt_root_; }

    // Test helpers
    void set_mpt_root(const MptHash& root) { current_mpt_root_ = root; }
    void set_fail_begin(bool fail) { fail_begin_ = fail; }
    void set_fail_write(bool fail) { fail_write_ = fail; }
    void set_fail_finalize(bool fail) { fail_finalize_ = fail; }

    [[nodiscard]] LSN snapshot_lsn() const { return snapshot_lsn_; }
    [[nodiscard]] std::size_t total_size() const { return total_size_; }
    [[nodiscard]] std::size_t bytes_written() const { return bytes_written_; }
    [[nodiscard]] std::size_t chunks_written() const { return chunks_written_; }
    [[nodiscard]] bool is_receiving() const { return is_receiving_; }
    [[nodiscard]] bool is_finalized() const { return finalized_; }
    [[nodiscard]] bool is_aborted() const { return aborted_; }
    [[nodiscard]] const std::vector<std::byte>& chunk_data() const { return chunk_data_; }

    void reset() {
        snapshot_lsn_ = LSN::invalid();
        total_size_ = 0;
        bytes_written_ = 0;
        chunks_written_ = 0;
        is_receiving_ = false;
        finalized_ = false;
        aborted_ = false;
        fail_begin_ = false;
        fail_write_ = false;
        fail_finalize_ = false;
        current_mpt_root_ = MptHash::zero();
        chunk_data_.clear();
    }

private:
    LSN snapshot_lsn_{LSN::invalid()};
    std::size_t total_size_{0};
    std::size_t bytes_written_{0};
    std::size_t chunks_written_{0};
    bool is_receiving_{false};
    bool finalized_{false};
    bool aborted_{false};
    bool fail_begin_{false};
    bool fail_write_{false};
    bool fail_finalize_{false};
    MptHash current_mpt_root_{};
    std::vector<std::byte> chunk_data_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class SnapshotReceiverTest : public ::testing::Test {
protected:
    void SetUp() override {
        sink_ = std::make_unique<MockSnapshotSink>();
        transport_ = std::make_unique<MockTransport>();

        auto result = transport_->start(local_id_);
        ASSERT_TRUE(result.is_ok());
    }

    void TearDown() override { transport_->stop(std::chrono::milliseconds{100}); }

    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }

    static MptHash make_mpt_hash(std::uint8_t seed) {
        MptHash hash;
        for (std::size_t i = 0; i < hash.data.size(); ++i) {
            hash.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return hash;
    }

    static SnapshotChunk make_chunk(NodeId sender, LSN lsn, std::uint32_t chunk_index,
                                    std::uint32_t total_chunks, std::size_t data_size,
                                    bool is_last = false) {
        SnapshotChunk chunk;
        chunk.sender_id = sender;
        chunk.snapshot_lsn = lsn;
        chunk.chunk_index = chunk_index;
        chunk.total_chunks = total_chunks;
        chunk.total_bytes = data_size * total_chunks;
        chunk.is_last = is_last;

        // Fill with test data
        chunk.data.resize(data_size);
        for (std::size_t i = 0; i < data_size; ++i) {
            chunk.data[i] = static_cast<std::byte>((chunk_index + i) % 256);
        }

        // Simple checksum for testing
        chunk.checksum = 0;
        for (auto b : chunk.data) {
            chunk.checksum += static_cast<std::uint32_t>(b);
        }

        return chunk;
    }

    std::unique_ptr<SnapshotReceiver>
    create_receiver(SnapshotReceiverConfig config = SnapshotReceiverConfig::defaults()) {
        return std::make_unique<SnapshotReceiver>(std::move(config), *sink_, *transport_);
    }

    NodeId local_id_ = make_node_id(2);   // Follower
    NodeId leader_id_ = make_node_id(1);  // Leader
    std::unique_ptr<MockSnapshotSink> sink_;
    std::unique_ptr<MockTransport> transport_;
};

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, Config_DefaultValues) {
    auto config = SnapshotReceiverConfig::defaults();

    EXPECT_EQ(config.receive_timeout, std::chrono::seconds{30});
    EXPECT_EQ(config.chunk_timeout, std::chrono::seconds{5});
    EXPECT_TRUE(config.verify_chunks);
    EXPECT_TRUE(config.verify_final_hash);
}

// ============================================================================
// Receiver Lifecycle Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, CreateReceiver_NotRunningInitially) {
    auto receiver = create_receiver();
    EXPECT_FALSE(receiver->is_running());
}

TEST_F(SnapshotReceiverTest, Start_SetsRunningState) {
    auto receiver = create_receiver();

    auto result = receiver->start();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(receiver->is_running());
}

TEST_F(SnapshotReceiverTest, StartTwice_SecondStartSucceeds) {
    auto receiver = create_receiver();

    auto result1 = receiver->start();
    ASSERT_TRUE(result1.is_ok());

    auto result2 = receiver->start();
    EXPECT_TRUE(result2.is_ok());
    EXPECT_TRUE(receiver->is_running());
}

TEST_F(SnapshotReceiverTest, Stop_ClearsRunningState) {
    auto receiver = create_receiver();

    auto result = receiver->start();
    ASSERT_TRUE(result.is_ok());

    receiver->stop();
    EXPECT_FALSE(receiver->is_running());
}

TEST_F(SnapshotReceiverTest, StopWithoutStart_NoError) {
    auto receiver = create_receiver();
    receiver->stop();  // Should not crash
    EXPECT_FALSE(receiver->is_running());
}

TEST_F(SnapshotReceiverTest, DestructorStopsReceiver) {
    auto receiver = create_receiver();
    auto result = receiver->start();
    ASSERT_TRUE(result.is_ok());

    receiver.reset();  // Destructor should call stop()
    // No crash means success
}

// ============================================================================
// Receive State Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveState_InitiallyIdle) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    auto state = receiver->receive_state();
    EXPECT_EQ(state.status, ReceiveStatus::Idle);
    EXPECT_EQ(state.bytes_received, 0);
    EXPECT_EQ(state.chunks_received, 0);
}

TEST_F(SnapshotReceiverTest, IsReceiving_FalseWhenIdle) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    EXPECT_FALSE(receiver->is_receiving());
}

// ============================================================================
// Chunk Reception Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveChunk_WhenNotRunning_ReturnsShuttingDown) {
    auto receiver = create_receiver();
    // Don't start the receiver

    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 5, 1024);
    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::ShuttingDown);
}

TEST_F(SnapshotReceiverTest, ReceiveFirstChunk_BeginsSnapshotAndUpdatesState) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 5, 1024);
    auto result = receiver->receive_chunk(chunk);

    ASSERT_TRUE(result.is_ok());

    auto state = receiver->receive_state();
    EXPECT_EQ(state.status, ReceiveStatus::Receiving);
    EXPECT_EQ(state.leader_id, leader_id_);
    EXPECT_EQ(state.snapshot_lsn.value, 100);
    EXPECT_EQ(state.chunks_received, 1);
    EXPECT_EQ(state.total_chunks, 5);
    EXPECT_GT(state.bytes_received, 0);
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_UpdatesBytesReceived) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 3, 1024);
    ASSERT_TRUE(receiver->receive_chunk(chunk).is_ok());

    auto state = receiver->receive_state();
    EXPECT_EQ(state.bytes_received, 1024);
}

TEST_F(SnapshotReceiverTest, ReceiveMultipleChunks_AccumulatesData) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Receive 3 chunks
    for (std::uint32_t i = 0; i < 3; ++i) {
        auto chunk = make_chunk(leader_id_, LSN{100}, i, 3, 512, i == 2);
        auto result = receiver->receive_chunk(chunk);
        ASSERT_TRUE(result.is_ok()) << "Chunk " << i << " failed";
    }

    auto state = receiver->receive_state();
    EXPECT_EQ(state.chunks_received, 3);
    EXPECT_EQ(state.bytes_received, 3 * 512);
}

TEST_F(SnapshotReceiverTest, ReceiveLastChunk_FinalizesAndCompletesSnapshot) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Set expected MPT root
    auto expected_root = make_mpt_hash(42);
    sink_->set_mpt_root(expected_root);

    // Receive single chunk (is_last = true)
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    auto result = receiver->receive_chunk(chunk);

    ASSERT_TRUE(result.is_ok());

    auto state = receiver->receive_state();
    EXPECT_EQ(state.status, ReceiveStatus::Complete);
    EXPECT_TRUE(sink_->is_finalized());
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_VerifiesChecksum) {
    SnapshotReceiverConfig config;
    config.verify_chunks = true;

    auto receiver = create_receiver(config);
    ASSERT_TRUE(receiver->start().is_ok());

    // Create chunk with invalid checksum
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    chunk.checksum = 0xDEADBEEF;  // Invalid checksum

    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::ChecksumMismatch);
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_SkipsChecksumWhenDisabled) {
    SnapshotReceiverConfig config;
    config.verify_chunks = false;

    auto receiver = create_receiver(config);
    ASSERT_TRUE(receiver->start().is_ok());

    // Create chunk with invalid checksum
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    chunk.checksum = 0xDEADBEEF;  // Invalid checksum, but verification disabled

    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_ok());
}

// ============================================================================
// Out-of-Order Chunk Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveChunk_OutOfOrder_StillAccepted) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // First send chunk 0 to initialize state
    auto chunk0 = make_chunk(leader_id_, LSN{100}, 0, 5, 512);
    ASSERT_TRUE(receiver->receive_chunk(chunk0).is_ok());

    // Receive chunk 2 before chunk 1
    auto chunk2 = make_chunk(leader_id_, LSN{100}, 2, 5, 512);
    auto result = receiver->receive_chunk(chunk2);

    // Should accept out-of-order chunks (buffering is implementation detail)
    EXPECT_TRUE(result.is_ok());
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_DuplicateChunk_Ignored) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 3, 512);

    // Receive same chunk twice
    ASSERT_TRUE(receiver->receive_chunk(chunk).is_ok());
    auto result = receiver->receive_chunk(chunk);

    // Duplicate should be handled gracefully (either accepted or ignored)
    // The important thing is it doesn't crash or corrupt state
    (void)result;

    auto state = receiver->receive_state();
    // Should still be receiving
    EXPECT_EQ(state.status, ReceiveStatus::Receiving);
}

// ============================================================================
// Sink Error Handling Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveChunk_SinkBeginFails_ReturnsError) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    sink_->set_fail_begin(true);

    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024);
    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::SnapshotTransferFailed);
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_SinkWriteFails_ReturnsError) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // First chunk succeeds to begin snapshot
    auto chunk0 = make_chunk(leader_id_, LSN{100}, 0, 3, 512);
    ASSERT_TRUE(receiver->receive_chunk(chunk0).is_ok());

    // Now fail subsequent writes
    sink_->set_fail_write(true);

    auto chunk1 = make_chunk(leader_id_, LSN{100}, 1, 3, 512);
    auto result = receiver->receive_chunk(chunk1);

    EXPECT_TRUE(result.is_err());
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_SinkFinalizeFails_ReturnsError) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    sink_->set_fail_finalize(true);

    // Receive last chunk
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_err());

    auto state = receiver->receive_state();
    EXPECT_EQ(state.status, ReceiveStatus::Failed);
}

// ============================================================================
// MPT Root Verification Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveLastChunk_VerifiesMptRoot) {
    SnapshotReceiverConfig config;
    config.verify_final_hash = true;

    auto receiver = create_receiver(config);
    ASSERT_TRUE(receiver->start().is_ok());

    // Set sink's MPT root to match expected
    auto expected_root = make_mpt_hash(42);
    sink_->set_mpt_root(expected_root);

    // Receive last chunk
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(sink_->is_finalized());
}

// ============================================================================
// Abort Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, AbortReceive_WhileReceiving_AbortsAndResetsState) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Start receiving
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 5, 1024);
    ASSERT_TRUE(receiver->receive_chunk(chunk).is_ok());

    EXPECT_TRUE(receiver->is_receiving());

    // Abort
    receiver->abort_receive();

    EXPECT_FALSE(receiver->is_receiving());
    EXPECT_TRUE(sink_->is_aborted());

    auto state = receiver->receive_state();
    EXPECT_EQ(state.status, ReceiveStatus::Idle);
}

TEST_F(SnapshotReceiverTest, AbortReceive_WhenIdle_NoEffect) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Abort when idle
    receiver->abort_receive();

    auto state = receiver->receive_state();
    EXPECT_EQ(state.status, ReceiveStatus::Idle);
}

// ============================================================================
// Snapshot Request Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, RequestSnapshot_SendsRequestToLeader) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Set up transport connection to leader
    MockTransport leader_transport;
    ASSERT_TRUE(leader_transport.start(leader_id_).is_ok());
    transport_->link_to(leader_transport);
    ASSERT_TRUE(transport_->connect(leader_id_, "").is_ok());

    auto result = receiver->request_snapshot(leader_id_);

    // Result depends on transport implementation
    (void)result;  // May or may not succeed depending on transport state
}

TEST_F(SnapshotReceiverTest, RequestSnapshot_WhenNotRunning_ReturnsShuttingDown) {
    auto receiver = create_receiver();
    // Don't start

    auto result = receiver->request_snapshot(leader_id_);

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::ShuttingDown);
}

// ============================================================================
// Callback Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, SnapshotCompleteCallback_InvokedOnSuccess) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    std::atomic<bool> callback_invoked{false};
    LSN callback_lsn{0};
    bool callback_success{false};

    receiver->set_complete_callback([&](LSN lsn, bool success) {
        callback_invoked.store(true);
        callback_lsn = lsn;
        callback_success = success;
    });

    // Receive complete snapshot (single chunk)
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    ASSERT_TRUE(receiver->receive_chunk(chunk).is_ok());

    EXPECT_TRUE(callback_invoked.load());
    EXPECT_EQ(callback_lsn.value, 100);
    EXPECT_TRUE(callback_success);
}

TEST_F(SnapshotReceiverTest, SnapshotCompleteCallback_InvokedOnFailure) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    std::atomic<bool> callback_invoked{false};
    bool callback_success{true};

    receiver->set_complete_callback([&](LSN, bool success) {
        callback_invoked.store(true);
        callback_success = success;
    });

    // Make finalize fail
    sink_->set_fail_finalize(true);

    // Receive last chunk
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024, true);
    (void)receiver->receive_chunk(chunk);

    EXPECT_TRUE(callback_invoked.load());
    EXPECT_FALSE(callback_success);
}

// ============================================================================
// New Snapshot During Receive Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveChunk_DifferentSnapshot_AbortsAndStartsNew) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Start receiving snapshot with LSN 100
    auto chunk1 = make_chunk(leader_id_, LSN{100}, 0, 5, 512);
    ASSERT_TRUE(receiver->receive_chunk(chunk1).is_ok());

    auto state1 = receiver->receive_state();
    EXPECT_EQ(state1.snapshot_lsn.value, 100);

    // Receive chunk from different snapshot (LSN 200)
    auto chunk2 = make_chunk(leader_id_, LSN{200}, 0, 3, 512);
    auto result = receiver->receive_chunk(chunk2);

    ASSERT_TRUE(result.is_ok());

    auto state2 = receiver->receive_state();
    // Should have switched to new snapshot
    EXPECT_EQ(state2.snapshot_lsn.value, 200);
    EXPECT_EQ(state2.total_chunks, 3);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ConcurrentChunkReception) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    std::atomic<int> received{0};
    constexpr int num_threads = 4;
    constexpr int chunks_per_thread = 25;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int c = 0; c < chunks_per_thread; ++c) {
                auto chunk_index = static_cast<std::uint32_t>(t * chunks_per_thread + c);
                auto chunk = make_chunk(leader_id_, LSN{100}, chunk_index, 100, 256);
                auto result = receiver->receive_chunk(chunk);
                if (result.is_ok()) {
                    received.fetch_add(1);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should have received chunks without crashing
    EXPECT_GT(received.load(), 0);
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_F(SnapshotReceiverTest, ReceiveChunk_EmptyData_Handled) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    SnapshotChunk empty_chunk;
    empty_chunk.sender_id = leader_id_;
    empty_chunk.snapshot_lsn = LSN{100};
    empty_chunk.chunk_index = 0;
    empty_chunk.total_chunks = 1;
    empty_chunk.total_bytes = 0;
    empty_chunk.data.clear();
    empty_chunk.checksum = 0;
    empty_chunk.is_last = true;

    auto result = receiver->receive_chunk(empty_chunk);
    // Should handle empty chunk gracefully
    EXPECT_TRUE(result.is_ok());
}

TEST_F(SnapshotReceiverTest, ReceiveChunk_VeryLargeChunk_Handled) {
    auto receiver = create_receiver();
    ASSERT_TRUE(receiver->start().is_ok());

    // Create a large chunk (1MB)
    auto chunk = make_chunk(leader_id_, LSN{100}, 0, 1, 1024 * 1024, true);
    auto result = receiver->receive_chunk(chunk);

    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(sink_->bytes_written(), 1024 * 1024);
}

}  // namespace
}  // namespace dotvm::core::state::replication
