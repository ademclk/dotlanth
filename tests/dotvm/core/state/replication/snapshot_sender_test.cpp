/// @file snapshot_sender_test.cpp
/// @brief Tests for SnapshotSender snapshot transfer component

#include "dotvm/core/state/replication/snapshot_sender.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Mock SnapshotSource Implementation
// ============================================================================

/// @brief Mock implementation of SnapshotSource for testing
class MockSnapshotSource : public SnapshotSource {
public:
    MockSnapshotSource() = default;

    [[nodiscard]] state::LSN snapshot_lsn() const override {
        std::lock_guard lock(mtx_);
        return snapshot_lsn_;
    }

    [[nodiscard]] std::size_t total_size() const override {
        std::lock_guard lock(mtx_);
        return snapshot_data_.size();
    }

    [[nodiscard]] Result<std::vector<std::byte>> read_chunk(std::size_t offset,
                                                            std::size_t size) const override {
        std::lock_guard lock(mtx_);

        if (fail_reads_) {
            return ReplicationError::SnapshotTransferFailed;
        }

        if (offset >= snapshot_data_.size()) {
            return std::vector<std::byte>{};
        }

        std::size_t actual_size = std::min(size, snapshot_data_.size() - offset);
        std::vector<std::byte> chunk(snapshot_data_.begin() + static_cast<std::ptrdiff_t>(offset),
                                     snapshot_data_.begin() + static_cast<std::ptrdiff_t>(offset + actual_size));
        return chunk;
    }

    [[nodiscard]] MptHash mpt_root() const override {
        std::lock_guard lock(mtx_);
        return mpt_root_;
    }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    void set_snapshot_data(std::vector<std::byte> data) {
        std::lock_guard lock(mtx_);
        snapshot_data_ = std::move(data);
    }

    void set_snapshot_lsn(state::LSN lsn) {
        std::lock_guard lock(mtx_);
        snapshot_lsn_ = lsn;
    }

    void set_mpt_root(MptHash root) {
        std::lock_guard lock(mtx_);
        mpt_root_ = root;
    }

    void set_fail_reads(bool fail) {
        std::lock_guard lock(mtx_);
        fail_reads_ = fail;
    }

private:
    mutable std::mutex mtx_;
    std::vector<std::byte> snapshot_data_;
    state::LSN snapshot_lsn_{0};
    MptHash mpt_root_;
    bool fail_reads_{false};
};

// ============================================================================
// Mock Transport Implementation
// ============================================================================

/// @brief Simplified mock transport for SnapshotSender testing
class TestMockTransport : public Transport {
public:
    TestMockTransport() = default;

    [[nodiscard]] Result<void> start(NodeId local_id) override {
        std::lock_guard lock(mtx_);
        local_id_ = local_id;
        running_ = true;
        return {};
    }

    void stop(std::chrono::milliseconds /*timeout*/) override {
        std::lock_guard lock(mtx_);
        running_ = false;
    }

    [[nodiscard]] bool is_running() const noexcept override { return running_.load(); }

    [[nodiscard]] Result<void> connect(const NodeId& peer, std::string_view /*address*/) override {
        std::lock_guard lock(mtx_);
        connected_peers_.insert(peer);
        return {};
    }

    [[nodiscard]] Result<void> disconnect(const NodeId& peer, bool /*graceful*/) override {
        std::lock_guard lock(mtx_);
        connected_peers_.erase(peer);
        return {};
    }

    [[nodiscard]] ConnectionState get_state(const NodeId& peer) const noexcept override {
        std::lock_guard lock(mtx_);
        if (connected_peers_.contains(peer)) {
            return ConnectionState::Connected;
        }
        return ConnectionState::Disconnected;
    }

    [[nodiscard]] std::optional<ConnectionStats> get_stats(const NodeId& /*peer*/) const
        noexcept override {
        return std::nullopt;
    }

    [[nodiscard]] std::vector<NodeId> connected_peers() const override {
        std::lock_guard lock(mtx_);
        return {connected_peers_.begin(), connected_peers_.end()};
    }

    [[nodiscard]] Result<void> send(const NodeId& to, StreamType stream,
                                    std::span<const std::byte> data) override {
        std::lock_guard lock(mtx_);

        if (fail_sends_) {
            return ReplicationError::ConnectionClosed;
        }

        sent_messages_.push_back({to, stream, {data.begin(), data.end()}});
        bytes_sent_ += data.size();
        messages_sent_++;

        return {};
    }

    [[nodiscard]] std::size_t broadcast(StreamType stream, std::span<const std::byte> data,
                                        std::optional<NodeId> exclude) override {
        std::lock_guard lock(mtx_);
        std::size_t count = 0;

        for (const auto& peer : connected_peers_) {
            if (exclude && *exclude == peer) {
                continue;
            }
            sent_messages_.push_back({peer, stream, {data.begin(), data.end()}});
            count++;
        }

        return count;
    }

    void set_message_callback(MessageCallback callback) override {
        std::lock_guard lock(mtx_);
        message_callback_ = std::move(callback);
    }

    void set_connection_callback(ConnectionCallback callback) override {
        std::lock_guard lock(mtx_);
        connection_callback_ = std::move(callback);
    }

    [[nodiscard]] const TransportConfig& config() const noexcept override { return config_; }

    [[nodiscard]] NodeId local_id() const noexcept override { return local_id_; }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    struct SentMessage {
        NodeId to;
        StreamType stream;
        std::vector<std::byte> data;
    };

    void set_fail_sends(bool fail) {
        std::lock_guard lock(mtx_);
        fail_sends_ = fail;
    }

    std::vector<SentMessage> get_sent_messages() const {
        std::lock_guard lock(mtx_);
        return sent_messages_;
    }

    void clear_sent_messages() {
        std::lock_guard lock(mtx_);
        sent_messages_.clear();
    }

    std::size_t get_messages_sent() const {
        std::lock_guard lock(mtx_);
        return messages_sent_;
    }

    std::size_t get_bytes_sent() const {
        std::lock_guard lock(mtx_);
        return bytes_sent_;
    }

    void add_connected_peer(const NodeId& peer) {
        std::lock_guard lock(mtx_);
        connected_peers_.insert(peer);
    }

private:
    mutable std::mutex mtx_;
    TransportConfig config_;
    NodeId local_id_;
    std::atomic<bool> running_{false};
    std::set<NodeId> connected_peers_;
    std::vector<SentMessage> sent_messages_;
    std::size_t messages_sent_{0};
    std::size_t bytes_sent_{0};
    bool fail_sends_{false};
    MessageCallback message_callback_;
    ConnectionCallback connection_callback_;
};

// ============================================================================
// Test Fixture
// ============================================================================

class SnapshotSenderTest : public ::testing::Test {
protected:
    void SetUp() override {
        source_ = std::make_unique<MockSnapshotSource>();
        transport_ = std::make_unique<TestMockTransport>();
        transport_->start(make_node_id(0));
    }

    void TearDown() override { transport_->stop(std::chrono::milliseconds{100}); }

    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }

    static std::vector<std::byte> make_snapshot_data(std::size_t size) {
        std::vector<std::byte> data(size);
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<std::byte>(i % 256);
        }
        return data;
    }

    static MptHash make_mpt_hash(std::uint8_t seed) {
        MptHash hash;
        for (std::size_t i = 0; i < hash.data.size(); ++i) {
            hash.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return hash;
    }

    std::unique_ptr<SnapshotSender> create_sender(SnapshotSenderConfig config = {}) {
        return std::make_unique<SnapshotSender>(config, *source_, *transport_);
    }

    std::unique_ptr<MockSnapshotSource> source_;
    std::unique_ptr<TestMockTransport> transport_;
};

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(SnapshotSenderTest, DefaultConfigValues) {
    SnapshotSenderConfig config;

    EXPECT_EQ(config.chunk_size, 64 * 1024);  // 64KB
    EXPECT_EQ(config.max_concurrent_transfers, 2);
    EXPECT_EQ(config.transfer_timeout.count(), 30000);  // 30 seconds
    EXPECT_TRUE(config.verify_chunks);
}

TEST_F(SnapshotSenderTest, DefaultsFactoryMethod) {
    auto config = SnapshotSenderConfig::defaults();

    EXPECT_EQ(config.chunk_size, 64 * 1024);
    EXPECT_EQ(config.max_concurrent_transfers, 2);
}

// ============================================================================
// Sender Lifecycle Tests
// ============================================================================

TEST_F(SnapshotSenderTest, DefaultConstruction) {
    auto sender = create_sender();
    EXPECT_FALSE(sender->is_running());
}

TEST_F(SnapshotSenderTest, StartStop) {
    auto sender = create_sender();

    EXPECT_FALSE(sender->is_running());

    auto start_result = sender->start();
    ASSERT_TRUE(start_result.is_ok());
    EXPECT_TRUE(sender->is_running());

    sender->stop();
    EXPECT_FALSE(sender->is_running());
}

TEST_F(SnapshotSenderTest, DoubleStartIsNoOp) {
    auto sender = create_sender();

    auto result1 = sender->start();
    ASSERT_TRUE(result1.is_ok());
    EXPECT_TRUE(sender->is_running());

    auto result2 = sender->start();
    ASSERT_TRUE(result2.is_ok());
    EXPECT_TRUE(sender->is_running());
}

TEST_F(SnapshotSenderTest, StopWithoutStartIsNoOp) {
    auto sender = create_sender();
    EXPECT_FALSE(sender->is_running());

    sender->stop();
    EXPECT_FALSE(sender->is_running());
}

TEST_F(SnapshotSenderTest, DestructorStopsSender) {
    auto sender = create_sender();
    auto result = sender->start();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(sender->is_running());

    // Sender is destroyed here - should not crash
    sender.reset();
}

// ============================================================================
// Transfer Initiation Tests
// ============================================================================

TEST_F(SnapshotSenderTest, InitiateTransferSucceeds) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    auto result = sender->initiate_transfer(follower_id);
    ASSERT_TRUE(result.is_ok());

    auto state = sender->get_transfer_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->follower_id, follower_id);
    EXPECT_EQ(state->snapshot_lsn, state::LSN{100});
    EXPECT_EQ(state->total_size, 1024);
    EXPECT_EQ(state->bytes_sent, 0);
    EXPECT_EQ(state->status, TransferStatus::Pending);
}

TEST_F(SnapshotSenderTest, InitiateTransferWhenNotRunningFails) {
    auto sender = create_sender();
    // Don't start the sender

    auto follower_id = make_node_id(1);
    auto result = sender->initiate_transfer(follower_id);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::ShuttingDown);
}

TEST_F(SnapshotSenderTest, InitiateTransferForExistingFollowerFails) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    auto result1 = sender->initiate_transfer(follower_id);
    ASSERT_TRUE(result1.is_ok());

    auto result2 = sender->initiate_transfer(follower_id);
    ASSERT_TRUE(result2.is_err());
    EXPECT_EQ(result2.error(), ReplicationError::NodeAlreadyExists);
}

TEST_F(SnapshotSenderTest, InitiateTransferExceedsMaxConcurrent) {
    SnapshotSenderConfig config;
    config.max_concurrent_transfers = 2;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    // Start 2 transfers (max)
    ASSERT_TRUE(sender->initiate_transfer(make_node_id(1)).is_ok());
    ASSERT_TRUE(sender->initiate_transfer(make_node_id(2)).is_ok());

    // Third should fail
    auto result = sender->initiate_transfer(make_node_id(3));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::BackpressureExceeded);
}

// ============================================================================
// Transfer Processing Tests
// ============================================================================

TEST_F(SnapshotSenderTest, ProcessTransfersSendsChunks) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));  // 4 chunks of 256 bytes
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Process one chunk
    auto chunks_sent = sender->process_transfers();
    EXPECT_EQ(chunks_sent, 1);

    auto state = sender->get_transfer_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->bytes_sent, 256);
    EXPECT_EQ(state->chunks_sent, 1);
    EXPECT_EQ(state->status, TransferStatus::InProgress);
}

TEST_F(SnapshotSenderTest, ProcessTransfersCompletesTransfer) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(512));  // 2 chunks
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Process until completion
    std::size_t total_chunks = 0;
    for (int i = 0; i < 5 && sender->get_transfer_state(follower_id)->status != TransferStatus::Completed; ++i) {
        total_chunks += sender->process_transfers();
    }

    EXPECT_EQ(total_chunks, 2);

    auto state = sender->get_transfer_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->bytes_sent, 512);
    EXPECT_EQ(state->chunks_sent, 2);
    EXPECT_EQ(state->status, TransferStatus::Completed);
}

TEST_F(SnapshotSenderTest, ProcessTransfersUsesSnapshotStream) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(256));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    (void)sender->process_transfers();

    auto messages = transport_->get_sent_messages();
    ASSERT_FALSE(messages.empty());
    EXPECT_EQ(messages[0].stream, StreamType::Snapshot);
}

TEST_F(SnapshotSenderTest, ProcessTransfersWhenNotRunningReturnsZero) {
    auto sender = create_sender();
    // Don't start the sender

    auto chunks = sender->process_transfers();
    EXPECT_EQ(chunks, 0);
}

TEST_F(SnapshotSenderTest, ProcessTransfersWithNoActiveTransfersReturnsZero) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    auto chunks = sender->process_transfers();
    EXPECT_EQ(chunks, 0);
}

// ============================================================================
// Transfer Cancellation Tests
// ============================================================================

TEST_F(SnapshotSenderTest, CancelTransferSucceeds) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    auto result = sender->cancel_transfer(follower_id);
    ASSERT_TRUE(result.is_ok());

    auto state = sender->get_transfer_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->status, TransferStatus::Cancelled);
}

TEST_F(SnapshotSenderTest, CancelNonexistentTransferFails) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    auto follower_id = make_node_id(99);
    auto result = sender->cancel_transfer(follower_id);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::NodeNotFound);
}

TEST_F(SnapshotSenderTest, CancelledTransferNotProcessed) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Send one chunk
    (void)sender->process_transfers();
    auto state1 = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state1->chunks_sent, 1);

    // Cancel
    ASSERT_TRUE(sender->cancel_transfer(follower_id).is_ok());

    // Process again - should not send more chunks
    auto initial_messages = transport_->get_messages_sent();
    (void)sender->process_transfers();
    EXPECT_EQ(transport_->get_messages_sent(), initial_messages);
}

// ============================================================================
// Transfer State Tests
// ============================================================================

TEST_F(SnapshotSenderTest, GetTransferStateForNonexistentFollower) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    auto state = sender->get_transfer_state(make_node_id(99));
    EXPECT_FALSE(state.has_value());
}

TEST_F(SnapshotSenderTest, GetActiveTransfers) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    // Initially empty
    EXPECT_TRUE(sender->get_active_transfers().empty());

    // Add transfers
    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    ASSERT_TRUE(sender->initiate_transfer(id1).is_ok());
    ASSERT_TRUE(sender->initiate_transfer(id2).is_ok());

    auto active = sender->get_active_transfers();
    EXPECT_EQ(active.size(), 2);
}

TEST_F(SnapshotSenderTest, TransferStateTracksProgress) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));  // 4 chunks
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    auto state0 = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state0->total_chunks, 4);
    EXPECT_EQ(state0->chunks_sent, 0);

    (void)sender->process_transfers();

    auto state1 = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state1->chunks_sent, 1);
    EXPECT_EQ(state1->bytes_sent, 256);

    (void)sender->process_transfers();

    auto state2 = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state2->chunks_sent, 2);
    EXPECT_EQ(state2->bytes_sent, 512);
}

// ============================================================================
// Callback Tests
// ============================================================================

TEST_F(SnapshotSenderTest, TransferCompleteCallbackInvokedOnSuccess) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(256));  // 1 chunk
    source_->set_snapshot_lsn(state::LSN{100});

    std::atomic<bool> callback_called{false};
    NodeId callback_follower_id;
    bool callback_success = false;

    sender->set_transfer_complete_callback([&](const NodeId& id, bool success) {
        callback_follower_id = id;
        callback_success = success;
        callback_called.store(true);
    });

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Process until completion
    for (int i = 0; i < 5 && !callback_called.load(); ++i) {
        (void)sender->process_transfers();
    }

    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(callback_follower_id, follower_id);
    EXPECT_TRUE(callback_success);
}

TEST_F(SnapshotSenderTest, TransferCompleteCallbackInvokedOnCancellation) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    std::atomic<bool> callback_called{false};
    bool callback_success = true;  // Initialize to true to verify it's set to false

    sender->set_transfer_complete_callback([&](const NodeId&, bool success) {
        callback_success = success;
        callback_called.store(true);
    });

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    (void)sender->cancel_transfer(follower_id);

    EXPECT_TRUE(callback_called.load());
    EXPECT_FALSE(callback_success);
}

// ============================================================================
// Checksum Tests
// ============================================================================

TEST_F(SnapshotSenderTest, VerifyChunksEnabled) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;
    config.verify_chunks = true;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(256));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    (void)sender->process_transfers();

    // The chunk should have a non-zero checksum when verify_chunks is true
    // (This is verified through the sent message format)
    auto messages = transport_->get_sent_messages();
    ASSERT_FALSE(messages.empty());
    EXPECT_FALSE(messages[0].data.empty());
}

// ============================================================================
// Transport Failure Tests
// ============================================================================

TEST_F(SnapshotSenderTest, TransportFailureMarksTransferAsFailed) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Send one chunk successfully
    (void)sender->process_transfers();
    EXPECT_EQ(sender->get_transfer_state(follower_id)->status, TransferStatus::InProgress);

    // Make transport fail
    transport_->set_fail_sends(true);
    (void)sender->process_transfers();

    auto state = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state->status, TransferStatus::Failed);
}

TEST_F(SnapshotSenderTest, SourceReadFailureMarksTransferAsFailed) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(1024));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Send one chunk successfully
    (void)sender->process_transfers();

    // Make source fail
    source_->set_fail_reads(true);
    (void)sender->process_transfers();

    auto state = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state->status, TransferStatus::Failed);
}

// ============================================================================
// Concurrent Transfer Tests
// ============================================================================

TEST_F(SnapshotSenderTest, MultipleTransfersProcessedInParallel) {
    SnapshotSenderConfig config;
    config.chunk_size = 256;
    config.max_concurrent_transfers = 3;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(512));  // 2 chunks each
    source_->set_snapshot_lsn(state::LSN{100});

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    auto id3 = make_node_id(3);

    ASSERT_TRUE(sender->initiate_transfer(id1).is_ok());
    ASSERT_TRUE(sender->initiate_transfer(id2).is_ok());
    ASSERT_TRUE(sender->initiate_transfer(id3).is_ok());

    // All three transfers should progress
    auto chunks_sent = sender->process_transfers();
    EXPECT_EQ(chunks_sent, 3);  // One chunk per transfer

    // Verify all have progress
    EXPECT_EQ(sender->get_transfer_state(id1)->chunks_sent, 1);
    EXPECT_EQ(sender->get_transfer_state(id2)->chunks_sent, 1);
    EXPECT_EQ(sender->get_transfer_state(id3)->chunks_sent, 1);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(SnapshotSenderTest, EmptySnapshotCompleteImmediately) {
    auto sender = create_sender();
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data({});  // Empty snapshot
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Should complete immediately or after one process call
    (void)sender->process_transfers();

    auto state = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state->status, TransferStatus::Completed);
}

TEST_F(SnapshotSenderTest, LastChunkSmallerThanChunkSize) {
    SnapshotSenderConfig config;
    config.chunk_size = 300;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(500));  // 300 + 200
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // First chunk: 300 bytes
    (void)sender->process_transfers();
    EXPECT_EQ(sender->get_transfer_state(follower_id)->bytes_sent, 300);

    // Second chunk: 200 bytes
    (void)sender->process_transfers();
    auto state = sender->get_transfer_state(follower_id);
    EXPECT_EQ(state->bytes_sent, 500);
    EXPECT_EQ(state->status, TransferStatus::Completed);
}

TEST_F(SnapshotSenderTest, CompletedTransfersRemovedFromActiveList) {
    SnapshotSenderConfig config;
    config.chunk_size = 1024;  // Single chunk

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(512));
    source_->set_snapshot_lsn(state::LSN{100});

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(sender->initiate_transfer(follower_id).is_ok());

    // Initially active
    auto active_before = sender->get_active_transfers();
    EXPECT_EQ(active_before.size(), 1);

    // Complete the transfer
    (void)sender->process_transfers();

    // Completed transfers are not in active list
    auto active_after = sender->get_active_transfers();
    EXPECT_EQ(active_after.size(), 0);

    // But state is still retrievable
    EXPECT_TRUE(sender->get_transfer_state(follower_id).has_value());
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST_F(SnapshotSenderTest, ConcurrentInitiateAndProcess) {
    SnapshotSenderConfig config;
    config.chunk_size = 64;
    config.max_concurrent_transfers = 100;

    auto sender = create_sender(config);
    ASSERT_TRUE(sender->start().is_ok());

    source_->set_snapshot_data(make_snapshot_data(256));
    source_->set_snapshot_lsn(state::LSN{100});

    std::atomic<bool> stop{false};
    std::atomic<int> transfers_started{0};

    // Thread that initiates transfers
    std::thread initiator([&]() {
        for (int i = 1; i <= 50 && !stop.load(); ++i) {
            auto id = make_node_id(static_cast<std::uint8_t>(i));
            if (sender->initiate_transfer(id).is_ok()) {
                transfers_started.fetch_add(1);
            }
        }
    });

    // Thread that processes transfers
    std::thread processor([&]() {
        while (!stop.load()) {
            (void)sender->process_transfers();
            std::this_thread::yield();
        }
    });

    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stop.store(true);
    initiator.join();
    processor.join();

    EXPECT_GT(transfers_started.load(), 0);
}

}  // namespace
}  // namespace dotvm::core::state::replication
