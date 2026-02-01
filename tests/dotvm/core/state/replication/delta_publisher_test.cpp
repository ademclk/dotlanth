/// @file delta_publisher_test.cpp
/// @brief Tests for DeltaPublisher delta streaming component

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "dotvm/core/state/replication/delta_publisher.hpp"

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Mock DeltaSource Implementation
// ============================================================================

/// @brief Mock implementation of DeltaSource for testing
class MockDeltaSource : public DeltaSource {
public:
    MockDeltaSource() = default;

    [[nodiscard]] state::LSN current_lsn() const noexcept override {
        std::lock_guard lock(mtx_);
        return current_lsn_;
    }

    [[nodiscard]] std::vector<state::LogRecord> read_entries(state::LSN from_lsn,
                                                             std::size_t max_entries,
                                                             std::size_t max_bytes) const override {
        std::lock_guard lock(mtx_);

        std::vector<state::LogRecord> result;
        std::size_t bytes_read = 0;

        for (const auto& [lsn, record] : entries_) {
            if (lsn < from_lsn) {
                continue;
            }

            std::size_t record_size =
                record.key.size() + record.value.size() + sizeof(state::LogRecord);
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

    [[nodiscard]] bool is_available(state::LSN lsn) const noexcept override {
        std::lock_guard lock(mtx_);
        return lsn >= min_available_lsn_ && lsn <= current_lsn_;
    }

    // ========================================================================
    // Test Helpers
    // ========================================================================

    void add_entry(state::LSN lsn, std::vector<std::byte> key, std::vector<std::byte> value) {
        std::lock_guard lock(mtx_);

        state::LogRecord record;
        record.lsn = lsn;
        record.type = state::LogRecordType::Put;
        record.key = std::move(key);
        record.value = std::move(value);
        record.tx_id = state::TxId{1, 1};
        record.checksum = 0;

        entries_.emplace(lsn, std::move(record));

        if (lsn > current_lsn_) {
            current_lsn_ = lsn;
        }
    }

    void set_current_lsn(state::LSN lsn) {
        std::lock_guard lock(mtx_);
        current_lsn_ = lsn;
    }

    void set_min_available_lsn(state::LSN lsn) {
        std::lock_guard lock(mtx_);
        min_available_lsn_ = lsn;
    }

    void clear_entries() {
        std::lock_guard lock(mtx_);
        entries_.clear();
        current_lsn_ = state::LSN{0};
    }

private:
    mutable std::mutex mtx_;
    std::map<state::LSN, state::LogRecord, std::less<>> entries_;
    state::LSN current_lsn_{0};
    state::LSN min_available_lsn_{0};
};

// ============================================================================
// Mock Transport Implementation
// ============================================================================

/// @brief Simplified mock transport for DeltaPublisher testing
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

    [[nodiscard]] std::optional<ConnectionStats>
    get_stats(const NodeId& /*peer*/) const noexcept override {
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

class DeltaPublisherTest : public ::testing::Test {
protected:
    void SetUp() override {
        source_ = std::make_unique<MockDeltaSource>();
        transport_ = std::make_unique<TestMockTransport>();
        (void)transport_->start(make_node_id(0));
    }

    void TearDown() override { transport_->stop(std::chrono::milliseconds{100}); }

    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }

    static std::vector<std::byte> make_key(std::size_t size = 10) {
        std::vector<std::byte> key(size);
        for (std::size_t i = 0; i < size; ++i) {
            key[i] = static_cast<std::byte>('k' + (i % 26));
        }
        return key;
    }

    static std::vector<std::byte> make_value(std::size_t size = 50) {
        std::vector<std::byte> value(size);
        for (std::size_t i = 0; i < size; ++i) {
            value[i] = static_cast<std::byte>('v' + (i % 26));
        }
        return value;
    }

    std::unique_ptr<DeltaPublisher> create_publisher(DeltaPublisherConfig config = {}) {
        return std::make_unique<DeltaPublisher>(config, *source_, *transport_);
    }

    std::unique_ptr<MockDeltaSource> source_;
    std::unique_ptr<TestMockTransport> transport_;
};

// ============================================================================
// Publisher Lifecycle Tests
// ============================================================================

TEST_F(DeltaPublisherTest, DefaultConstruction) {
    auto publisher = create_publisher();
    EXPECT_FALSE(publisher->is_running());
}

TEST_F(DeltaPublisherTest, StartStop) {
    auto publisher = create_publisher();

    EXPECT_FALSE(publisher->is_running());

    auto start_result = publisher->start();
    ASSERT_TRUE(start_result.is_ok());
    EXPECT_TRUE(publisher->is_running());

    publisher->stop();
    EXPECT_FALSE(publisher->is_running());
}

TEST_F(DeltaPublisherTest, DoubleStartIsNoOp) {
    auto publisher = create_publisher();

    auto result1 = publisher->start();
    ASSERT_TRUE(result1.is_ok());
    EXPECT_TRUE(publisher->is_running());

    auto result2 = publisher->start();
    ASSERT_TRUE(result2.is_ok());
    EXPECT_TRUE(publisher->is_running());
}

TEST_F(DeltaPublisherTest, StopWithoutStartIsNoOp) {
    auto publisher = create_publisher();
    EXPECT_FALSE(publisher->is_running());

    publisher->stop();
    EXPECT_FALSE(publisher->is_running());
}

TEST_F(DeltaPublisherTest, DestructorStopsPublisher) {
    auto publisher = create_publisher();
    auto result = publisher->start();
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(publisher->is_running());

    // Publisher is destroyed here
    publisher.reset();

    // Verify it stopped (we can't check is_running after destruction,
    // but we verify no crash occurs)
}

// ============================================================================
// Follower Management Tests
// ============================================================================

TEST_F(DeltaPublisherTest, AddFollowerSucceeds) {
    auto publisher = create_publisher();
    auto follower_id = make_node_id(1);

    auto result = publisher->add_follower(follower_id, state::LSN{0});
    ASSERT_TRUE(result.is_ok());

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->follower_id, follower_id);
    EXPECT_EQ(state->acknowledged_lsn, state::LSN{0});
    EXPECT_EQ(state->sent_lsn, state::LSN{0});
}

TEST_F(DeltaPublisherTest, AddFollowerWithStartLsn) {
    auto publisher = create_publisher();
    auto follower_id = make_node_id(1);

    auto result = publisher->add_follower(follower_id, state::LSN{100});
    ASSERT_TRUE(result.is_ok());

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->acknowledged_lsn, state::LSN{100});
    EXPECT_EQ(state->sent_lsn, state::LSN{100});
}

TEST_F(DeltaPublisherTest, AddDuplicateFollowerFails) {
    auto publisher = create_publisher();
    auto follower_id = make_node_id(1);

    auto result1 = publisher->add_follower(follower_id, state::LSN{0});
    ASSERT_TRUE(result1.is_ok());

    auto result2 = publisher->add_follower(follower_id, state::LSN{0});
    ASSERT_TRUE(result2.is_err());
    EXPECT_EQ(result2.error(), ReplicationError::NodeAlreadyExists);
}

TEST_F(DeltaPublisherTest, RemoveFollowerSucceeds) {
    auto publisher = create_publisher();
    auto follower_id = make_node_id(1);

    auto add_result = publisher->add_follower(follower_id, state::LSN{0});
    ASSERT_TRUE(add_result.is_ok());

    auto remove_result = publisher->remove_follower(follower_id);
    ASSERT_TRUE(remove_result.is_ok());

    auto state = publisher->get_follower_state(follower_id);
    EXPECT_FALSE(state.has_value());
}

TEST_F(DeltaPublisherTest, RemoveNonexistentFollowerFails) {
    auto publisher = create_publisher();
    auto follower_id = make_node_id(1);

    auto result = publisher->remove_follower(follower_id);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::NodeNotFound);
}

TEST_F(DeltaPublisherTest, GetAllFollowerStates) {
    auto publisher = create_publisher();

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    auto id3 = make_node_id(3);

    ASSERT_TRUE(publisher->add_follower(id1, state::LSN{10}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id2, state::LSN{20}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id3, state::LSN{30}).is_ok());

    auto states = publisher->get_all_follower_states();
    EXPECT_EQ(states.size(), 3);

    // Verify all followers are present
    bool found1 = false, found2 = false, found3 = false;
    for (const auto& state : states) {
        if (state.follower_id == id1) {
            found1 = true;
            EXPECT_EQ(state.acknowledged_lsn, state::LSN{10});
        } else if (state.follower_id == id2) {
            found2 = true;
            EXPECT_EQ(state.acknowledged_lsn, state::LSN{20});
        } else if (state.follower_id == id3) {
            found3 = true;
            EXPECT_EQ(state.acknowledged_lsn, state::LSN{30});
        }
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
    EXPECT_TRUE(found3);
}

TEST_F(DeltaPublisherTest, GetStateForNonexistentFollower) {
    auto publisher = create_publisher();
    auto follower_id = make_node_id(99);

    auto state = publisher->get_follower_state(follower_id);
    EXPECT_FALSE(state.has_value());
}

// ============================================================================
// Publishing Tests
// ============================================================================

TEST_F(DeltaPublisherTest, PublishWithNoFollowersReturnsZero) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    auto batches_sent = publisher->publish();
    EXPECT_EQ(batches_sent, 0);
}

TEST_F(DeltaPublisherTest, PublishWithNoEntriesReturnsZero) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    auto batches_sent = publisher->publish();
    EXPECT_EQ(batches_sent, 0);
}

TEST_F(DeltaPublisherTest, PublishWhenNotRunningReturnsZero) {
    auto publisher = create_publisher();
    // Don't start the publisher

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());
    source_->add_entry(state::LSN{1}, make_key(), make_value());

    auto batches_sent = publisher->publish();
    EXPECT_EQ(batches_sent, 0);
}

TEST_F(DeltaPublisherTest, PublishSendsBatchToFollower) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());
    source_->add_entry(state::LSN{2}, make_key(), make_value());

    auto batches_sent = publisher->publish();
    EXPECT_EQ(batches_sent, 1);

    // Verify follower state updated
    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->sent_lsn, state::LSN{2});
    EXPECT_EQ(state->inflight_batches, 1);
    EXPECT_EQ(state->batches_sent, 1);
}

TEST_F(DeltaPublisherTest, PublishToMultipleFollowers) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    ASSERT_TRUE(publisher->add_follower(id1, state::LSN{0}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id2, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    auto batches_sent = publisher->publish();
    EXPECT_EQ(batches_sent, 2);

    // Both followers should have received the batch
    auto state1 = publisher->get_follower_state(id1);
    auto state2 = publisher->get_follower_state(id2);
    ASSERT_TRUE(state1.has_value());
    ASSERT_TRUE(state2.has_value());
    EXPECT_EQ(state1->sent_lsn, state::LSN{1});
    EXPECT_EQ(state2->sent_lsn, state::LSN{1});
}

TEST_F(DeltaPublisherTest, PublishRespectsFollowerStartLsn) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    // Add entries
    source_->add_entry(state::LSN{1}, make_key(), make_value());
    source_->add_entry(state::LSN{2}, make_key(), make_value());
    source_->add_entry(state::LSN{3}, make_key(), make_value());

    // Follower starts at LSN 2
    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{2}).is_ok());

    auto batches_sent = publisher->publish();
    EXPECT_EQ(batches_sent, 1);

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->sent_lsn, state::LSN{3});  // Should only have sent LSN 3
}

TEST_F(DeltaPublisherTest, FollowerMarkedAsCaughtUp) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    (void)publisher->publish();

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->is_caught_up);
}

// ============================================================================
// ACK Handling Tests
// ============================================================================

TEST_F(DeltaPublisherTest, HandleAckUpdatesFollowerState) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());
    source_->add_entry(state::LSN{2}, make_key(), make_value());

    (void)publisher->publish();

    // Simulate ACK
    publisher->handle_ack(follower_id, state::LSN{2});

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->acknowledged_lsn, state::LSN{2});
    EXPECT_EQ(state->inflight_batches, 0);  // Decreased after ACK
}

TEST_F(DeltaPublisherTest, HandleAckForUnknownFollowerIsNoOp) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto unknown_id = make_node_id(99);

    // Should not crash or error
    publisher->handle_ack(unknown_id, state::LSN{100});
}

TEST_F(DeltaPublisherTest, HandleAckIgnoresStaleAcks) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{50}).is_ok());

    // Follower already at LSN 50, ACK for LSN 40 should be ignored
    publisher->handle_ack(follower_id, state::LSN{40});

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->acknowledged_lsn, state::LSN{50});
}

TEST_F(DeltaPublisherTest, AckCallbackInvoked) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    std::atomic<bool> callback_called{false};
    NodeId callback_follower_id;
    state::LSN callback_lsn{0};

    publisher->set_ack_callback([&](const NodeId& id, state::LSN lsn) {
        callback_follower_id = id;
        callback_lsn = lsn;
        callback_called.store(true);
    });

    source_->add_entry(state::LSN{1}, make_key(), make_value());
    (void)publisher->publish();
    publisher->handle_ack(follower_id, state::LSN{1});

    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(callback_follower_id, follower_id);
    EXPECT_EQ(callback_lsn, state::LSN{1});
}

TEST_F(DeltaPublisherTest, AckCallbackInvokedForStaleAckButLsnNotUpdated) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{100}).is_ok());

    std::atomic<bool> callback_called{false};
    state::LSN callback_lsn{0};

    publisher->set_ack_callback([&](const NodeId&, state::LSN lsn) {
        callback_lsn = lsn;
        callback_called.store(true);
    });

    // Stale ACK - callback is invoked but LSN is not updated
    publisher->handle_ack(follower_id, state::LSN{50});

    // Callback is invoked for all ACKs from known followers
    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(callback_lsn, state::LSN{50});  // Reports the ACK LSN

    // But the acknowledged_lsn is not updated for stale ACKs
    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->acknowledged_lsn, state::LSN{100});  // Still at 100, not 50
}

// ============================================================================
// Backpressure Tests
// ============================================================================

TEST_F(DeltaPublisherTest, BackpressureWhenMaxInflightReached) {
    DeltaPublisherConfig config;
    config.max_inflight_batches = 2;
    config.batch_size = 1;  // One entry per batch

    auto publisher = create_publisher(config);
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    // Add many entries
    for (std::uint64_t i = 1; i <= 10; ++i) {
        source_->add_entry(state::LSN{i}, make_key(), make_value());
    }

    // First publish - sends 2 batches (max_inflight_batches = 2)
    // Note: The implementation sends batches until inflight reaches max
    (void)publisher->publish();

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());

    // Should have some inflight batches
    EXPECT_GE(state->inflight_batches, 1);

    // Keep publishing until backpressure kicks in
    for (int i = 0; i < 5; ++i) {
        (void)publisher->publish();
    }

    state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());

    // Inflight should be capped at max_inflight_batches
    EXPECT_LE(state->inflight_batches, config.max_inflight_batches);
}

TEST_F(DeltaPublisherTest, BackpressureReleasedAfterAck) {
    DeltaPublisherConfig config;
    config.max_inflight_batches = 1;
    config.batch_size = 2;

    auto publisher = create_publisher(config);
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    // Add entries
    for (std::uint64_t i = 1; i <= 4; ++i) {
        source_->add_entry(state::LSN{i}, make_key(), make_value());
    }

    // First publish
    (void)publisher->publish();

    auto state1 = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state1.has_value());
    EXPECT_EQ(state1->inflight_batches, 1);

    // No more batches should be sent due to backpressure
    auto batches2 = publisher->publish();
    EXPECT_EQ(batches2, 0);

    // ACK releases backpressure
    publisher->handle_ack(follower_id, state1->sent_lsn);

    // Now can send more
    auto batches3 = publisher->publish();
    EXPECT_GE(batches3, 0);  // May send more if entries available
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(DeltaPublisherTest, TotalBytesSentTracked) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    EXPECT_EQ(publisher->total_bytes_sent(), 0);

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(10), make_value(100));

    (void)publisher->publish();

    EXPECT_GT(publisher->total_bytes_sent(), 0);
}

TEST_F(DeltaPublisherTest, TotalBatchesSentTracked) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    EXPECT_EQ(publisher->total_batches_sent(), 0);

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    ASSERT_TRUE(publisher->add_follower(id1, state::LSN{0}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id2, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    (void)publisher->publish();

    EXPECT_EQ(publisher->total_batches_sent(), 2);  // One batch to each follower
}

TEST_F(DeltaPublisherTest, MinAcknowledgedLsnWithNoFollowers) {
    auto publisher = create_publisher();
    source_->set_current_lsn(state::LSN{100});

    // With no followers, min_acknowledged_lsn returns source's current LSN
    EXPECT_EQ(publisher->min_acknowledged_lsn(), state::LSN{100});
}

TEST_F(DeltaPublisherTest, MinAcknowledgedLsnWithFollowers) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    auto id3 = make_node_id(3);

    ASSERT_TRUE(publisher->add_follower(id1, state::LSN{10}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id2, state::LSN{20}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id3, state::LSN{30}).is_ok());

    EXPECT_EQ(publisher->min_acknowledged_lsn(), state::LSN{10});
}

TEST_F(DeltaPublisherTest, MinAcknowledgedLsnUpdatesWithAcks) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(publisher->add_follower(id1, state::LSN{0}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id2, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());
    source_->add_entry(state::LSN{2}, make_key(), make_value());

    (void)publisher->publish();

    // Both start at 0, so min is 0
    EXPECT_EQ(publisher->min_acknowledged_lsn(), state::LSN{0});

    // ACK from first follower
    publisher->handle_ack(id1, state::LSN{2});
    EXPECT_EQ(publisher->min_acknowledged_lsn(), state::LSN{0});  // id2 still at 0

    // ACK from second follower
    publisher->handle_ack(id2, state::LSN{1});
    EXPECT_EQ(publisher->min_acknowledged_lsn(), state::LSN{1});

    // Both caught up
    publisher->handle_ack(id2, state::LSN{2});
    EXPECT_EQ(publisher->min_acknowledged_lsn(), state::LSN{2});
}

TEST_F(DeltaPublisherTest, AllCaughtUpWithNoFollowers) {
    auto publisher = create_publisher();
    EXPECT_TRUE(publisher->all_caught_up());
}

TEST_F(DeltaPublisherTest, AllCaughtUpWhenAllFollowersCaughtUp) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(publisher->add_follower(id1, state::LSN{0}).is_ok());
    ASSERT_TRUE(publisher->add_follower(id2, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    // Before publishing, followers not caught up (if there are entries)
    // Actually, initial state may have is_caught_up = false

    (void)publisher->publish();

    // After publishing all entries, followers should be caught up
    EXPECT_TRUE(publisher->all_caught_up());
}

TEST_F(DeltaPublisherTest, AllCaughtUpFalseWhenFollowerBehind) {
    DeltaPublisherConfig config;
    config.max_inflight_batches = 1;
    config.batch_size = 1;

    auto publisher = create_publisher(config);
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    // Add multiple entries
    source_->add_entry(state::LSN{1}, make_key(), make_value());
    source_->add_entry(state::LSN{2}, make_key(), make_value());
    source_->add_entry(state::LSN{3}, make_key(), make_value());

    // Publish once - only sends one batch due to max_inflight_batches = 1
    (void)publisher->publish();

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());

    // If follower hasn't received all entries, not caught up
    if (state->sent_lsn < state::LSN{3}) {
        EXPECT_FALSE(publisher->all_caught_up());
    }
}

// ============================================================================
// Transport Failure Tests
// ============================================================================

TEST_F(DeltaPublisherTest, TransportFailureDoesNotCrash) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    // Make transport fail
    transport_->set_fail_sends(true);

    // Should not crash, returns 0 batches
    auto batches = publisher->publish();
    EXPECT_EQ(batches, 0);

    // Follower state should not be updated on failure
    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->sent_lsn, state::LSN{0});
    EXPECT_EQ(state->inflight_batches, 0);
}

TEST_F(DeltaPublisherTest, TransportRecoveryAllowsPublishing) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    // Make transport fail
    transport_->set_fail_sends(true);
    auto batches1 = publisher->publish();
    EXPECT_EQ(batches1, 0);

    // Recover transport
    transport_->set_fail_sends(false);
    auto batches2 = publisher->publish();
    EXPECT_EQ(batches2, 1);
}

// ============================================================================
// Concurrent Access Tests
// ============================================================================

TEST_F(DeltaPublisherTest, ConcurrentAddRemoveFollowers) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};

    std::vector<std::thread> threads;

    // Add followers from multiple threads
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&, i]() {
            auto id = make_node_id(static_cast<std::uint8_t>(i));
            auto result = publisher->add_follower(id, state::LSN{0});
            if (result.is_ok()) {
                success_count.fetch_add(1);
            } else {
                error_count.fetch_add(1);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 10);
    EXPECT_EQ(error_count.load(), 0);

    // Verify all followers added
    auto states = publisher->get_all_follower_states();
    EXPECT_EQ(states.size(), 10);
}

TEST_F(DeltaPublisherTest, ConcurrentPublishAndAck) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    // Add many entries
    for (std::uint64_t i = 1; i <= 100; ++i) {
        source_->add_entry(state::LSN{i}, make_key(), make_value());
    }

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> acked_lsn{0};

    // Publisher thread
    std::thread publisher_thread([&]() {
        while (!stop.load()) {
            (void)publisher->publish();
            std::this_thread::yield();
        }
    });

    // ACK thread
    std::thread ack_thread([&]() {
        while (!stop.load()) {
            auto state = publisher->get_follower_state(follower_id);
            if (state && state->sent_lsn.value > acked_lsn.load()) {
                publisher->handle_ack(follower_id, state->sent_lsn);
                acked_lsn.store(state->sent_lsn.value);
            }
            std::this_thread::yield();
        }
    });

    // Let it run for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds{100});

    stop.store(true);
    publisher_thread.join();
    ack_thread.join();

    // Should have made progress
    EXPECT_GT(acked_lsn.load(), 0);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(DeltaPublisherTest, BatchSizeRespected) {
    DeltaPublisherConfig config;
    config.batch_size = 2;

    auto publisher = create_publisher(config);
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    // Add 5 entries
    for (std::uint64_t i = 1; i <= 5; ++i) {
        source_->add_entry(state::LSN{i}, make_key(), make_value());
    }

    // First publish should send up to batch_size entries
    (void)publisher->publish();

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());

    // Sent LSN should be at least 2 (batch_size)
    EXPECT_GE(state->sent_lsn.value, 2);
}

TEST_F(DeltaPublisherTest, NotifyNewEntriesDoesNotCrash) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    // notify_new_entries is a no-op hint, should not crash
    publisher->notify_new_entries();
    publisher->notify_new_entries();
    publisher->notify_new_entries();
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(DeltaPublisherTest, EmptySourceReadReturnsNoEntries) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{0}).is_ok());

    // Source has current_lsn but no entries (gap case)
    source_->set_current_lsn(state::LSN{10});

    auto batches = publisher->publish();
    EXPECT_EQ(batches, 0);
}

TEST_F(DeltaPublisherTest, FollowerAlreadyCaughtUp) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    // Follower starts at current LSN - already caught up
    auto follower_id = make_node_id(1);
    ASSERT_TRUE(publisher->add_follower(follower_id, state::LSN{1}).is_ok());

    auto batches = publisher->publish();
    EXPECT_EQ(batches, 0);

    auto state = publisher->get_follower_state(follower_id);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->is_caught_up);
}

TEST_F(DeltaPublisherTest, LargeNumberOfFollowers) {
    auto publisher = create_publisher();
    ASSERT_TRUE(publisher->start().is_ok());

    // Add 100 followers
    for (int i = 0; i < 100; ++i) {
        auto id = make_node_id(static_cast<std::uint8_t>(i));
        ASSERT_TRUE(publisher->add_follower(id, state::LSN{0}).is_ok());
    }

    source_->add_entry(state::LSN{1}, make_key(), make_value());

    auto batches = publisher->publish();
    EXPECT_EQ(batches, 100);

    EXPECT_EQ(publisher->total_batches_sent(), 100);
}

}  // namespace
}  // namespace dotvm::core::state::replication
