/// @file transport_test.cpp
/// @brief Tests for replication transport layer

#include "dotvm/core/state/replication/transport.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace dotvm::core::state::replication {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class TransportTest : public ::testing::Test {
protected:
    static NodeId make_node_id(std::uint8_t seed) {
        NodeId id;
        for (std::size_t i = 0; i < id.data.size(); ++i) {
            id.data[i] = static_cast<std::uint8_t>((seed + i) % 256);
        }
        return id;
    }

    static std::vector<std::byte> make_test_message(std::size_t size = 100) {
        std::vector<std::byte> msg(size);
        for (std::size_t i = 0; i < size; ++i) {
            msg[i] = static_cast<std::byte>(i % 256);
        }
        return msg;
    }
};

// ============================================================================
// StreamType Tests
// ============================================================================

TEST(StreamTypeTest, ToStringCoversAll) {
    EXPECT_EQ(to_string(StreamType::Raft), "Raft");
    EXPECT_EQ(to_string(StreamType::Delta), "Delta");
    EXPECT_EQ(to_string(StreamType::Snapshot), "Snapshot");
    EXPECT_EQ(to_string(StreamType::Control), "Control");
}

TEST(StreamTypeTest, GetStreamTypeForMessages) {
    EXPECT_EQ(get_stream_type(MessageType::RequestVote), StreamType::Raft);
    EXPECT_EQ(get_stream_type(MessageType::AppendEntries), StreamType::Raft);

    EXPECT_EQ(get_stream_type(MessageType::DeltaBatch), StreamType::Delta);
    EXPECT_EQ(get_stream_type(MessageType::DeltaAck), StreamType::Delta);

    EXPECT_EQ(get_stream_type(MessageType::SnapshotChunk), StreamType::Snapshot);
    EXPECT_EQ(get_stream_type(MessageType::SnapshotRequest), StreamType::Snapshot);

    EXPECT_EQ(get_stream_type(MessageType::Heartbeat), StreamType::Control);
    EXPECT_EQ(get_stream_type(MessageType::ClusterConfig), StreamType::Control);
}

// ============================================================================
// ConnectionState Tests
// ============================================================================

TEST(ConnectionStateTest, ToStringCoversAll) {
    EXPECT_EQ(to_string(ConnectionState::Disconnected), "Disconnected");
    EXPECT_EQ(to_string(ConnectionState::Connecting), "Connecting");
    EXPECT_EQ(to_string(ConnectionState::Connected), "Connected");
    EXPECT_EQ(to_string(ConnectionState::Draining), "Draining");
    EXPECT_EQ(to_string(ConnectionState::Failed), "Failed");
}

// ============================================================================
// MockTransport Lifecycle Tests
// ============================================================================

TEST_F(TransportTest, MockTransportStartStop) {
    MockTransport transport;

    EXPECT_FALSE(transport.is_running());

    auto result = transport.start(make_node_id(1));
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(transport.is_running());

    transport.stop(std::chrono::milliseconds{100});
    EXPECT_FALSE(transport.is_running());
}

TEST_F(TransportTest, MockTransportLocalId) {
    MockTransport transport;
    auto id = make_node_id(42);

    EXPECT_TRUE(transport.local_id().is_null());  // Before start

    auto result = transport.start(id);
    ASSERT_TRUE(result.is_ok());

    EXPECT_EQ(transport.local_id(), id);
}

TEST_F(TransportTest, MockTransportConfig) {
    TransportConfig cfg;
    cfg.bind_port = 5000;
    cfg.connect_timeout = std::chrono::milliseconds{10000};

    MockTransport transport(cfg);
    EXPECT_EQ(transport.config().bind_port, 5000);
    EXPECT_EQ(transport.config().connect_timeout, std::chrono::milliseconds{10000});
}

// ============================================================================
// Connection Tests
// ============================================================================

TEST_F(TransportTest, ConnectToUnlinkedPeerFails) {
    MockTransport transport;
    auto result = transport.start(make_node_id(1));
    ASSERT_TRUE(result.is_ok());

    auto connect_result = transport.connect(make_node_id(2), "127.0.0.1:4000");
    EXPECT_TRUE(connect_result.is_err());
    EXPECT_EQ(connect_result.error(), ReplicationError::ConnectionFailed);
}

TEST_F(TransportTest, ConnectToLinkedPeerSucceeds) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);

    auto connect_result = transport1.connect(id2, "127.0.0.1:4000");
    ASSERT_TRUE(connect_result.is_ok());

    EXPECT_EQ(transport1.get_state(id2), ConnectionState::Connected);
}

TEST_F(TransportTest, DisconnectWorks) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());

    auto disconnect_result = transport1.disconnect(id2, true);
    ASSERT_TRUE(disconnect_result.is_ok());

    EXPECT_EQ(transport1.get_state(id2), ConnectionState::Disconnected);
}

TEST_F(TransportTest, ConnectedPeersList) {
    MockTransport transport1;
    MockTransport transport2;
    MockTransport transport3;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    auto id3 = make_node_id(3);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());
    ASSERT_TRUE(transport3.start(id3).is_ok());

    transport1.link_to(transport2);
    transport1.link_to(transport3);

    ASSERT_TRUE(transport1.connect(id2, "").is_ok());
    ASSERT_TRUE(transport1.connect(id3, "").is_ok());

    auto peers = transport1.connected_peers();
    EXPECT_EQ(peers.size(), 2);

    // Disconnect one
    auto disc_result = transport1.disconnect(id2, false);
    ASSERT_TRUE(disc_result.is_ok());

    peers = transport1.connected_peers();
    EXPECT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0], id3);
}

// ============================================================================
// Message Sending Tests
// ============================================================================

TEST_F(TransportTest, SendMessageToConnectedPeer) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);
    ASSERT_TRUE(transport2.connect(id1, "").is_ok());  // Both sides connected
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());

    std::atomic<bool> received{false};
    std::vector<std::byte> received_data;

    transport2.set_message_callback(
        [&](const NodeId& from, StreamType stream, std::span<const std::byte> data) {
            EXPECT_EQ(from, id1);
            EXPECT_EQ(stream, StreamType::Delta);
            received_data.assign(data.begin(), data.end());
            received.store(true);
        });

    auto msg = make_test_message(50);
    auto result = transport1.send(id2, StreamType::Delta, msg);
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(received.load());
    EXPECT_EQ(received_data, msg);
}

TEST_F(TransportTest, SendToDisconnectedPeerFails) {
    MockTransport transport;
    ASSERT_TRUE(transport.start(make_node_id(1)).is_ok());

    auto msg = make_test_message();
    auto result = transport.send(make_node_id(2), StreamType::Raft, msg);
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error(), ReplicationError::ConnectionClosed);
}

TEST_F(TransportTest, BroadcastToAllPeers) {
    MockTransport transport1;
    MockTransport transport2;
    MockTransport transport3;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    auto id3 = make_node_id(3);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());
    ASSERT_TRUE(transport3.start(id3).is_ok());

    transport1.link_to(transport2);
    transport1.link_to(transport3);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());
    ASSERT_TRUE(transport1.connect(id3, "").is_ok());

    std::atomic<int> receive_count{0};

    auto callback = [&](const NodeId&, StreamType, std::span<const std::byte>) {
        receive_count.fetch_add(1);
    };

    transport2.set_message_callback(callback);
    transport3.set_message_callback(callback);

    auto msg = make_test_message();
    auto count = transport1.broadcast(StreamType::Control, msg);

    EXPECT_EQ(count, 2);
    EXPECT_EQ(receive_count.load(), 2);
}

TEST_F(TransportTest, BroadcastWithExclude) {
    MockTransport transport1;
    MockTransport transport2;
    MockTransport transport3;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);
    auto id3 = make_node_id(3);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());
    ASSERT_TRUE(transport3.start(id3).is_ok());

    transport1.link_to(transport2);
    transport1.link_to(transport3);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());
    ASSERT_TRUE(transport1.connect(id3, "").is_ok());

    std::atomic<int> receive_count{0};

    transport2.set_message_callback([&](const NodeId&, StreamType, std::span<const std::byte>) {
        receive_count.fetch_add(1);
    });

    transport3.set_message_callback([&](const NodeId&, StreamType, std::span<const std::byte>) {
        receive_count.fetch_add(1);
    });

    auto msg = make_test_message();
    auto count = transport1.broadcast(StreamType::Control, msg, id2);  // Exclude id2

    EXPECT_EQ(count, 1);
    EXPECT_EQ(receive_count.load(), 1);
}

// ============================================================================
// Fault Injection Tests
// ============================================================================

TEST_F(TransportTest, PartitionDropsMessages) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());
    ASSERT_TRUE(transport2.connect(id1, "").is_ok());

    std::atomic<int> receive_count{0};

    transport2.set_message_callback([&](const NodeId&, StreamType, std::span<const std::byte>) {
        receive_count.fetch_add(1);
    });

    // Send before partition
    auto msg = make_test_message();
    auto send_result = transport1.send(id2, StreamType::Control, msg);
    ASSERT_TRUE(send_result.is_ok());
    EXPECT_EQ(receive_count.load(), 1);

    // Partition
    transport1.partition_from(id2);

    // Send during partition
    send_result = transport1.send(id2, StreamType::Control, msg);
    ASSERT_TRUE(send_result.is_ok());  // Send succeeds but message dropped
    EXPECT_EQ(receive_count.load(), 1);  // No increase

    // Heal partition
    transport1.heal_partition(id2);

    // Send after healing
    send_result = transport1.send(id2, StreamType::Control, msg);
    ASSERT_TRUE(send_result.is_ok());
    EXPECT_EQ(receive_count.load(), 2);
}

TEST_F(TransportTest, DropRateAffectsDelivery) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());

    std::atomic<int> receive_count{0};

    transport2.set_message_callback([&](const NodeId&, StreamType, std::span<const std::byte>) {
        receive_count.fetch_add(1);
    });

    transport1.set_drop_rate(0.5);  // 50% drop rate

    auto msg = make_test_message();
    int sent = 100;
    for (int i = 0; i < sent; ++i) {
        auto result = transport1.send(id2, StreamType::Control, msg);
        (void)result;  // Ignore result, we're testing drop rate
    }

    // Should receive roughly half (with some variance)
    int received = receive_count.load();
    EXPECT_GT(received, 20);  // At least some got through
    EXPECT_LT(received, 80);  // But not all
}

// ============================================================================
// Statistics Tests
// ============================================================================

TEST_F(TransportTest, MessageCountTracking) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());

    auto msg = make_test_message();
    auto r1 = transport1.send(id2, StreamType::Control, msg);
    auto r2 = transport1.send(id2, StreamType::Control, msg);
    auto r3 = transport1.send(id2, StreamType::Control, msg);
    (void)r1;
    (void)r2;
    (void)r3;

    EXPECT_EQ(transport1.messages_sent(), 3);
    EXPECT_EQ(transport2.messages_received(), 3);
}

TEST_F(TransportTest, ConnectionStats) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);
    ASSERT_TRUE(transport1.connect(id2, "").is_ok());

    auto msg = make_test_message(100);
    auto send_result = transport1.send(id2, StreamType::Control, msg);
    ASSERT_TRUE(send_result.is_ok());

    auto stats = transport1.get_stats(id2);
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->messages_sent, 1);
    EXPECT_EQ(stats->bytes_sent, 100);
}

// ============================================================================
// Connection Callback Tests
// ============================================================================

TEST_F(TransportTest, ConnectionCallbackOnConnect) {
    MockTransport transport1;
    MockTransport transport2;

    auto id1 = make_node_id(1);
    auto id2 = make_node_id(2);

    ASSERT_TRUE(transport1.start(id1).is_ok());
    ASSERT_TRUE(transport2.start(id2).is_ok());

    transport1.link_to(transport2);

    std::atomic<bool> connect_callback_called{false};
    ConnectionState observed_old_state{ConnectionState::Failed};  // Sentinel value
    ConnectionState observed_new_state{ConnectionState::Disconnected};

    transport1.set_connection_callback(
        [&](const NodeId& peer, ConnectionState old_state, ConnectionState new_state,
            std::optional<ReplicationError> error) {
            // Only check the first callback (connection event), ignore disconnect from cleanup
            if (new_state == ConnectionState::Connected && !connect_callback_called.load()) {
                EXPECT_EQ(peer, id2);
                EXPECT_EQ(old_state, ConnectionState::Disconnected);
                EXPECT_FALSE(error.has_value());
                observed_old_state = old_state;
                observed_new_state = new_state;
                connect_callback_called.store(true);
            }
        });

    auto result = transport1.connect(id2, "");
    ASSERT_TRUE(result.is_ok());

    EXPECT_TRUE(connect_callback_called.load());
    EXPECT_EQ(observed_old_state, ConnectionState::Disconnected);
    EXPECT_EQ(observed_new_state, ConnectionState::Connected);
}

TEST_F(TransportTest, ConnectionCallbackOnFailure) {
    MockTransport transport;
    ASSERT_TRUE(transport.start(make_node_id(1)).is_ok());

    std::atomic<bool> callback_called{false};
    std::optional<ReplicationError> observed_error;

    transport.set_connection_callback(
        [&](const NodeId&, ConnectionState, ConnectionState new_state,
            std::optional<ReplicationError> error) {
            if (new_state == ConnectionState::Failed) {
                observed_error = error;
                callback_called.store(true);
            }
        });

    // Try to connect to unlinked peer
    auto result = transport.connect(make_node_id(99), "");
    (void)result;  // We expect it to fail

    EXPECT_TRUE(callback_called.load());
    ASSERT_TRUE(observed_error.has_value());
    EXPECT_EQ(observed_error.value(), ReplicationError::ConnectionFailed);
}

}  // namespace
}  // namespace dotvm::core::state::replication
