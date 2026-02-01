/// @file fault_injector_test.cpp
/// @brief Tests for replication fault injection utility

#include "fault_injector.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace dotvm::core::state::replication::testing {
namespace {

// ============================================================================
// Test Fixtures
// ============================================================================

class FaultInjectorTest : public ::testing::Test {
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

    FaultInjector injector_;
};

// ============================================================================
// FaultType Tests
// ============================================================================

TEST(FaultTypeTest, EnumValuesExist) {
    // Verify enum values exist and have expected values
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::None), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::MessageDrop), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::MessageDelay), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::MessageCorrupt), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::PartialPartition), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::NodeCrash), 5);
    EXPECT_EQ(static_cast<std::uint8_t>(FaultType::SlowNode), 6);
}

// ============================================================================
// FaultConfig Tests
// ============================================================================

TEST(FaultConfigTest, DefaultConstruction) {
    FaultConfig config;

    EXPECT_EQ(config.type, FaultType::None);
    EXPECT_DOUBLE_EQ(config.probability, 0.0);
    EXPECT_EQ(config.delay.count(), 0);
    EXPECT_FALSE(config.target_node.has_value());
    EXPECT_FALSE(config.target_stream.has_value());
    EXPECT_EQ(config.max_occurrences, std::numeric_limits<std::size_t>::max());
}

// ============================================================================
// FaultInjector Basic Tests
// ============================================================================

TEST_F(FaultInjectorTest, DefaultState) {
    EXPECT_FALSE(injector_.is_enabled());
    EXPECT_EQ(injector_.messages_dropped(), 0);
    EXPECT_EQ(injector_.messages_delayed(), 0);
    EXPECT_EQ(injector_.messages_corrupted(), 0);
}

TEST_F(FaultInjectorTest, EnableDisable) {
    EXPECT_FALSE(injector_.is_enabled());

    injector_.enable();
    EXPECT_TRUE(injector_.is_enabled());

    injector_.disable();
    EXPECT_FALSE(injector_.is_enabled());
}

TEST_F(FaultInjectorTest, ClearFaults) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;

    injector_.add_fault(config);
    injector_.enable();

    // Process a message (should be dropped)
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, make_test_message());
    EXPECT_FALSE(result.has_value());

    // Clear faults
    injector_.clear_faults();

    // Now message should pass through
    result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, make_test_message());
    EXPECT_TRUE(result.has_value());
}

TEST_F(FaultInjectorTest, ResetStats) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;

    injector_.add_fault(config);
    injector_.enable();

    // Drop some messages
    (void)injector_.process_message(make_node_id(1), make_node_id(2), StreamType::Delta, make_test_message());
    (void)injector_.process_message(make_node_id(1), make_node_id(2), StreamType::Delta, make_test_message());

    EXPECT_EQ(injector_.messages_dropped(), 2);

    injector_.reset_stats();
    EXPECT_EQ(injector_.messages_dropped(), 0);
}

// ============================================================================
// Message Drop Tests
// ============================================================================

TEST_F(FaultInjectorTest, MessageDropWithProbability100) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;  // 100% drop rate

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(injector_.messages_dropped(), 1);
}

TEST_F(FaultInjectorTest, MessageDropWithProbability0) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 0.0;  // 0% drop rate

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), msg);
    EXPECT_EQ(injector_.messages_dropped(), 0);
}

TEST_F(FaultInjectorTest, MessageDropTargetSpecificNode) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;
    config.target_node = make_node_id(2);  // Only target node 2

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();

    // Message to node 2 should be dropped
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);
    EXPECT_FALSE(result.has_value());

    // Message to node 3 should pass through
    result = injector_.process_message(
        make_node_id(1), make_node_id(3), StreamType::Delta, msg);
    EXPECT_TRUE(result.has_value());
}

TEST_F(FaultInjectorTest, MessageDropTargetSpecificStream) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;
    config.target_stream = StreamType::Delta;  // Only drop Delta messages

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();

    // Delta messages should be dropped
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);
    EXPECT_FALSE(result.has_value());

    // Raft messages should pass through
    result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Raft, msg);
    EXPECT_TRUE(result.has_value());
}

TEST_F(FaultInjectorTest, MessageDropMaxOccurrences) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;
    config.max_occurrences = 2;  // Only drop 2 messages

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();

    // First two should be dropped
    EXPECT_FALSE(injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg).has_value());
    EXPECT_FALSE(injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg).has_value());

    // Third should pass through
    EXPECT_TRUE(injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg).has_value());

    EXPECT_EQ(injector_.messages_dropped(), 2);
}

// ============================================================================
// Message Delay Tests
// ============================================================================

TEST_F(FaultInjectorTest, MessageDelayAppliesDelay) {
    FaultConfig config;
    config.type = FaultType::MessageDelay;
    config.probability = 1.0;
    config.delay = std::chrono::milliseconds{50};

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();
    auto start = std::chrono::steady_clock::now();

    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);

    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), msg);
    EXPECT_GE(elapsed, std::chrono::milliseconds{45});  // Allow some tolerance
    EXPECT_EQ(injector_.messages_delayed(), 1);
}

// ============================================================================
// Message Corruption Tests
// ============================================================================

TEST_F(FaultInjectorTest, MessageCorruptModifiesData) {
    FaultConfig config;
    config.type = FaultType::MessageCorrupt;
    config.probability = 1.0;

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message(100);
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), msg.size());
    EXPECT_NE(result.value(), msg);  // Data should be different
    EXPECT_EQ(injector_.messages_corrupted(), 1);
}

// ============================================================================
// Partial Partition Tests
// ============================================================================

TEST_F(FaultInjectorTest, PartialPartitionAsymmetric) {
    FaultConfig config;
    config.type = FaultType::PartialPartition;
    config.probability = 1.0;
    config.target_node = make_node_id(2);  // Partition node 2

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();

    // Messages TO node 2 should be dropped
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);
    EXPECT_FALSE(result.has_value());

    // Messages FROM node 2 should pass (asymmetric)
    result = injector_.process_message(
        make_node_id(2), make_node_id(1), StreamType::Delta, msg);
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// Disabled Injector Tests
// ============================================================================

TEST_F(FaultInjectorTest, DisabledInjectorPassesThrough) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;

    injector_.add_fault(config);
    // Note: injector is NOT enabled

    auto msg = make_test_message();
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), msg);
}

// ============================================================================
// Multiple Faults Tests
// ============================================================================

TEST_F(FaultInjectorTest, MultipleFaultsApplyInOrder) {
    // Add delay then corruption
    FaultConfig delay_config;
    delay_config.type = FaultType::MessageDelay;
    delay_config.probability = 1.0;
    delay_config.delay = std::chrono::milliseconds{10};
    delay_config.target_stream = StreamType::Raft;

    FaultConfig corrupt_config;
    corrupt_config.type = FaultType::MessageCorrupt;
    corrupt_config.probability = 1.0;
    corrupt_config.target_stream = StreamType::Delta;

    injector_.add_fault(delay_config);
    injector_.add_fault(corrupt_config);
    injector_.enable();

    auto msg = make_test_message();

    // Delta message should be corrupted but not delayed
    auto result = injector_.process_message(
        make_node_id(1), make_node_id(2), StreamType::Delta, msg);
    EXPECT_TRUE(result.has_value());
    EXPECT_NE(result.value(), msg);
    EXPECT_EQ(injector_.messages_corrupted(), 1);
    EXPECT_EQ(injector_.messages_delayed(), 0);
}

// ============================================================================
// Helper Function Tests
// ============================================================================

TEST(FaultHelperTest, DropMessagesToNode) {
    auto config = drop_messages_to(NodeId{}, 0.5);

    EXPECT_EQ(config.type, FaultType::MessageDrop);
    EXPECT_DOUBLE_EQ(config.probability, 0.5);
    EXPECT_TRUE(config.target_node.has_value());
}

TEST(FaultHelperTest, DelayAllMessages) {
    auto config = delay_all_messages(std::chrono::milliseconds{100});

    EXPECT_EQ(config.type, FaultType::MessageDelay);
    EXPECT_DOUBLE_EQ(config.probability, 1.0);
    EXPECT_EQ(config.delay, std::chrono::milliseconds{100});
    EXPECT_FALSE(config.target_node.has_value());
}

TEST(FaultHelperTest, CorruptDeltaStream) {
    auto config = corrupt_delta_stream(0.25);

    EXPECT_EQ(config.type, FaultType::MessageCorrupt);
    EXPECT_DOUBLE_EQ(config.probability, 0.25);
    EXPECT_TRUE(config.target_stream.has_value());
    EXPECT_EQ(config.target_stream.value(), StreamType::Delta);
}

// ============================================================================
// FaultableTransport Tests
// ============================================================================

class FaultableTransportTest : public ::testing::Test {
protected:
    void SetUp() override {
        transport1_ = std::make_unique<MockTransport>();
        transport2_ = std::make_unique<MockTransport>();

        id1_ = make_node_id(1);
        id2_ = make_node_id(2);

        ASSERT_TRUE(transport1_->start(id1_).is_ok());
        ASSERT_TRUE(transport2_->start(id2_).is_ok());

        transport1_->link_to(*transport2_);
        ASSERT_TRUE(transport1_->connect(id2_, "").is_ok());
    }

    void TearDown() override {
        transport1_->stop(std::chrono::milliseconds{100});
        transport2_->stop(std::chrono::milliseconds{100});
    }

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

    std::unique_ptr<MockTransport> transport1_;
    std::unique_ptr<MockTransport> transport2_;
    NodeId id1_;
    NodeId id2_;
    FaultInjector injector_;
};

TEST_F(FaultableTransportTest, PassThroughWhenNoFaults) {
    FaultableTransport faultable(*transport1_, injector_, id1_);

    std::atomic<bool> received{false};
    transport2_->set_message_callback(
        [&](const NodeId&, StreamType, std::span<const std::byte>) {
            received.store(true);
        });

    auto msg = make_test_message();
    auto result = faultable.send(id2_, StreamType::Delta, msg);

    EXPECT_TRUE(result.is_ok());
    EXPECT_TRUE(received.load());
}

TEST_F(FaultableTransportTest, DropsMessagesWhenFaultEnabled) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 1.0;

    injector_.add_fault(config);
    injector_.enable();

    FaultableTransport faultable(*transport1_, injector_, id1_);

    std::atomic<bool> received{false};
    transport2_->set_message_callback(
        [&](const NodeId&, StreamType, std::span<const std::byte>) {
            received.store(true);
        });

    auto msg = make_test_message();
    auto result = faultable.send(id2_, StreamType::Delta, msg);

    EXPECT_TRUE(result.is_ok());  // Send "succeeds" but message is dropped
    EXPECT_FALSE(received.load());
}

TEST_F(FaultableTransportTest, DelegatesAllTransportMethods) {
    FaultableTransport faultable(*transport1_, injector_, id1_);

    // Test various passthrough methods
    EXPECT_TRUE(faultable.is_running());
    EXPECT_EQ(faultable.local_id(), id1_);
    EXPECT_EQ(faultable.get_state(id2_), ConnectionState::Connected);

    auto peers = faultable.connected_peers();
    EXPECT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0], id2_);

    auto stats = faultable.get_stats(id2_);
    EXPECT_TRUE(stats.has_value());
}

// ============================================================================
// Probabilistic Drop Rate Tests
// ============================================================================

TEST_F(FaultInjectorTest, ProbabilisticDropAppliesRandomly) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = 0.5;  // 50% drop rate

    injector_.add_fault(config);
    injector_.enable();

    auto msg = make_test_message();
    int dropped = 0;
    const int iterations = 1000;

    for (int i = 0; i < iterations; ++i) {
        auto result = injector_.process_message(
            make_node_id(1), make_node_id(2), StreamType::Delta, msg);
        if (!result.has_value()) {
            ++dropped;
        }
    }

    // Should be roughly 50% with some statistical margin
    // Using chi-squared critical values, 5% significance
    double ratio = static_cast<double>(dropped) / iterations;
    EXPECT_GT(ratio, 0.35);  // Should be above 35%
    EXPECT_LT(ratio, 0.65);  // Should be below 65%
}

}  // namespace
}  // namespace dotvm::core::state::replication::testing
