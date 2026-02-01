#pragma once

/// @file fault_injector.hpp
/// @brief Chaos engineering utility for testing replication resilience
///
/// Provides fault injection capabilities for testing the replication layer's
/// behavior under adverse network conditions including message drops, delays,
/// corruption, and network partitions.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"
#include "dotvm/core/state/replication/transport.hpp"

namespace dotvm::core::state::replication::testing {

// ============================================================================
// Fault Type Enumeration
// ============================================================================

/// @brief Types of faults that can be injected
enum class FaultType : std::uint8_t {
    None = 0,             ///< No fault (passthrough)
    MessageDrop = 1,      ///< Drop messages randomly
    MessageDelay = 2,     ///< Add latency to messages
    MessageCorrupt = 3,   ///< Corrupt message data
    PartialPartition = 4, ///< Asymmetric partition (one-way)
    NodeCrash = 5,        ///< Simulate node failure
    SlowNode = 6,         ///< Slow down a node's processing
};

// ============================================================================
// Fault Configuration
// ============================================================================

/// @brief Configuration for a single fault injection rule
struct FaultConfig {
    FaultType type{FaultType::None};
    double probability{0.0};                 ///< 0.0 - 1.0 for random faults
    std::chrono::milliseconds delay{0};      ///< For MessageDelay
    std::optional<NodeId> target_node;       ///< Target specific node (nullopt = all)
    std::optional<StreamType> target_stream; ///< Target specific stream
    std::size_t max_occurrences{std::numeric_limits<std::size_t>::max()};
};

// ============================================================================
// Fault Injector
// ============================================================================

/// @brief Chaos engineering utility for injecting faults into message processing
///
/// FaultInjector allows configuring various fault scenarios and applying them
/// to messages during testing. Multiple faults can be configured and they are
/// evaluated in order.
///
/// Thread-safety: All public methods are thread-safe.
///
/// @par Usage Example
/// @code
/// FaultInjector injector;
/// injector.add_fault(drop_messages_to(node2, 0.5));  // 50% drop rate to node2
/// injector.enable();
///
/// // In message processing:
/// auto result = injector.process_message(from, to, stream, data);
/// if (!result) {
///     // Message was dropped
/// }
/// @endcode
class FaultInjector {
public:
    /// @brief Construct a fault injector
    FaultInjector() : rng_(std::random_device{}()) {}

    /// @brief Add a fault configuration
    ///
    /// Faults are evaluated in the order they are added. The first matching
    /// fault that triggers (based on probability) will be applied.
    ///
    /// @param config Fault configuration to add
    void add_fault(FaultConfig config) {
        std::lock_guard lock(mtx_);
        faults_.push_back(std::move(config));
        occurrence_counts_.push_back(std::make_unique<std::atomic<std::size_t>>(0));
    }

    /// @brief Remove all fault configurations
    void clear_faults() {
        std::lock_guard lock(mtx_);
        faults_.clear();
        occurrence_counts_.clear();
    }

    /// @brief Enable fault injection
    void enable() { enabled_.store(true, std::memory_order_release); }

    /// @brief Disable fault injection
    void disable() { enabled_.store(false, std::memory_order_release); }

    /// @brief Check if fault injection is enabled
    [[nodiscard]] bool is_enabled() const {
        return enabled_.load(std::memory_order_acquire);
    }

    /// @brief Process a message through the fault injector
    ///
    /// Evaluates all configured faults against the message. Returns the
    /// (potentially modified) message, or nullopt if the message should
    /// be dropped.
    ///
    /// @param from Source node ID
    /// @param to Destination node ID
    /// @param stream Stream type
    /// @param data Message data
    /// @return Modified message data, or nullopt if dropped
    [[nodiscard]] std::optional<std::vector<std::byte>> process_message(
        const NodeId& from,
        const NodeId& to,
        StreamType stream,
        std::span<const std::byte> data) {

        // If disabled, pass through unchanged
        if (!is_enabled()) {
            return std::vector<std::byte>(data.begin(), data.end());
        }

        std::lock_guard lock(mtx_);

        // Start with a copy of the data
        std::vector<std::byte> result(data.begin(), data.end());

        for (std::size_t i = 0; i < faults_.size(); ++i) {
            auto& fault = faults_[i];
            auto& count = occurrence_counts_[i];

            // Check if fault applies to this message
            if (!fault_matches(fault, from, to, stream)) {
                continue;
            }

            // Check max occurrences
            if (count->load(std::memory_order_relaxed) >= fault.max_occurrences) {
                continue;
            }

            // Check probability
            if (!should_trigger(fault.probability)) {
                continue;
            }

            // Apply the fault
            auto fault_result = apply_fault(fault, *count, from, to, stream, result);
            if (!fault_result.has_value()) {
                // Message dropped
                return std::nullopt;
            }
            result = std::move(fault_result.value());
        }

        return result;
    }

    /// @brief Get count of dropped messages
    [[nodiscard]] std::size_t messages_dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// @brief Get count of delayed messages
    [[nodiscard]] std::size_t messages_delayed() const {
        return delayed_.load(std::memory_order_relaxed);
    }

    /// @brief Get count of corrupted messages
    [[nodiscard]] std::size_t messages_corrupted() const {
        return corrupted_.load(std::memory_order_relaxed);
    }

    /// @brief Reset all statistics counters
    void reset_stats() {
        dropped_.store(0, std::memory_order_relaxed);
        delayed_.store(0, std::memory_order_relaxed);
        corrupted_.store(0, std::memory_order_relaxed);

        std::lock_guard lock(mtx_);
        for (auto& count : occurrence_counts_) {
            count->store(0, std::memory_order_relaxed);
        }
    }

private:
    /// @brief Check if a fault configuration matches the message
    [[nodiscard]] bool fault_matches(
        const FaultConfig& fault,
        const NodeId& from,
        const NodeId& to,
        StreamType stream) const {

        // Check target node (destination for most faults, source for PartialPartition)
        if (fault.target_node.has_value()) {
            if (fault.type == FaultType::PartialPartition) {
                // PartialPartition targets messages TO the node
                if (to != fault.target_node.value()) {
                    return false;
                }
            } else {
                // Other faults target messages TO the specified node
                if (to != fault.target_node.value()) {
                    return false;
                }
            }
        }

        // Check target stream
        if (fault.target_stream.has_value()) {
            if (stream != fault.target_stream.value()) {
                return false;
            }
        }

        return true;
    }

    /// @brief Check if fault should trigger based on probability
    [[nodiscard]] bool should_trigger(double probability) {
        if (probability >= 1.0) {
            return true;
        }
        if (probability <= 0.0) {
            return false;
        }

        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(rng_) < probability;
    }

    /// @brief Apply a fault to a message
    [[nodiscard]] std::optional<std::vector<std::byte>> apply_fault(
        const FaultConfig& fault,
        std::atomic<std::size_t>& count,
        const NodeId& /*from*/,
        const NodeId& /*to*/,
        StreamType /*stream*/,
        std::vector<std::byte>& data) {

        count.fetch_add(1, std::memory_order_relaxed);

        switch (fault.type) {
            case FaultType::None:
                return data;

            case FaultType::MessageDrop:
            case FaultType::PartialPartition:
            case FaultType::NodeCrash:
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return std::nullopt;

            case FaultType::MessageDelay:
            case FaultType::SlowNode:
                if (fault.delay.count() > 0) {
                    std::this_thread::sleep_for(fault.delay);
                }
                delayed_.fetch_add(1, std::memory_order_relaxed);
                return data;

            case FaultType::MessageCorrupt:
                corrupt_data(data);
                corrupted_.fetch_add(1, std::memory_order_relaxed);
                return data;
        }

        return data;
    }

    /// @brief Corrupt message data by flipping random bits
    void corrupt_data(std::vector<std::byte>& data) {
        if (data.empty()) {
            return;
        }

        // Flip 1-5 random bytes
        std::uniform_int_distribution<std::size_t> count_dist(1, std::min<std::size_t>(5, data.size()));
        std::uniform_int_distribution<std::size_t> pos_dist(0, data.size() - 1);
        std::uniform_int_distribution<std::uint8_t> byte_dist(1, 255);

        std::size_t num_corruptions = count_dist(rng_);
        for (std::size_t i = 0; i < num_corruptions; ++i) {
            std::size_t pos = pos_dist(rng_);
            // XOR with random non-zero value to ensure change
            data[pos] = static_cast<std::byte>(
                static_cast<std::uint8_t>(data[pos]) ^ byte_dist(rng_));
        }
    }

    std::vector<FaultConfig> faults_;
    std::vector<std::unique_ptr<std::atomic<std::size_t>>> occurrence_counts_;
    std::atomic<bool> enabled_{false};
    std::atomic<std::size_t> dropped_{0};
    std::atomic<std::size_t> delayed_{0};
    std::atomic<std::size_t> corrupted_{0};
    mutable std::mutex mtx_;
    std::mt19937 rng_;
};

// ============================================================================
// Faultable Transport Wrapper
// ============================================================================

/// @brief Transport wrapper that injects faults into message delivery
///
/// FaultableTransport wraps an existing Transport implementation and passes
/// all messages through a FaultInjector before delivery.
class FaultableTransport : public Transport {
public:
    /// @brief Construct a faultable transport
    ///
    /// @param inner The underlying transport to wrap
    /// @param injector The fault injector to use
    /// @param local_id This node's ID (for from field in process_message)
    FaultableTransport(Transport& inner, FaultInjector& injector, NodeId local_id)
        : inner_(inner), injector_(injector), local_id_(local_id) {}

    // ========================================================================
    // Transport Interface Implementation
    // ========================================================================

    [[nodiscard]] Result<void> start(NodeId local_id) override {
        local_id_ = local_id;
        return inner_.start(local_id);
    }

    void stop(std::chrono::milliseconds timeout) override {
        inner_.stop(timeout);
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return inner_.is_running();
    }

    [[nodiscard]] Result<void> connect(const NodeId& peer, std::string_view address) override {
        return inner_.connect(peer, address);
    }

    [[nodiscard]] Result<void> disconnect(const NodeId& peer, bool graceful) override {
        return inner_.disconnect(peer, graceful);
    }

    [[nodiscard]] ConnectionState get_state(const NodeId& peer) const noexcept override {
        return inner_.get_state(peer);
    }

    [[nodiscard]] std::optional<ConnectionStats> get_stats(const NodeId& peer) const noexcept override {
        return inner_.get_stats(peer);
    }

    [[nodiscard]] std::vector<NodeId> connected_peers() const override {
        return inner_.connected_peers();
    }

    [[nodiscard]] Result<void> send(
        const NodeId& to,
        StreamType stream,
        std::span<const std::byte> data) override {

        // Process through fault injector
        auto result = injector_.process_message(local_id_, to, stream, data);

        if (!result.has_value()) {
            // Message was dropped - return success (fault injection should be invisible)
            return {};
        }

        // Send the (potentially modified) message
        return inner_.send(to, stream, result.value());
    }

    [[nodiscard]] std::size_t broadcast(
        StreamType stream,
        std::span<const std::byte> data,
        std::optional<NodeId> exclude = std::nullopt) override {

        std::size_t count = 0;
        for (const auto& peer : inner_.connected_peers()) {
            if (exclude.has_value() && peer == exclude.value()) {
                continue;
            }

            auto result = send(peer, stream, data);
            if (result.is_ok()) {
                ++count;
            }
        }
        return count;
    }

    void set_message_callback(MessageCallback callback) override {
        inner_.set_message_callback(std::move(callback));
    }

    void set_connection_callback(ConnectionCallback callback) override {
        inner_.set_connection_callback(std::move(callback));
    }

    [[nodiscard]] const TransportConfig& config() const noexcept override {
        return inner_.config();
    }

    [[nodiscard]] NodeId local_id() const noexcept override {
        return local_id_;
    }

private:
    Transport& inner_;
    FaultInjector& injector_;
    NodeId local_id_;
};

// ============================================================================
// Helper Functions for Common Fault Scenarios
// ============================================================================

/// @brief Create a fault config that drops messages to a specific node
///
/// @param target The node to target
/// @param probability Drop probability (0.0 - 1.0)
/// @return Configured FaultConfig
[[nodiscard]] inline FaultConfig drop_messages_to(
    const NodeId& target,
    double probability) {

    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = probability;
    config.target_node = target;
    return config;
}

/// @brief Create a fault config that delays all messages
///
/// @param delay The delay to apply
/// @return Configured FaultConfig
[[nodiscard]] inline FaultConfig delay_all_messages(
    std::chrono::milliseconds delay) {

    FaultConfig config;
    config.type = FaultType::MessageDelay;
    config.probability = 1.0;
    config.delay = delay;
    return config;
}

/// @brief Create a fault config that corrupts Delta stream messages
///
/// @param probability Corruption probability (0.0 - 1.0)
/// @return Configured FaultConfig
[[nodiscard]] inline FaultConfig corrupt_delta_stream(double probability) {
    FaultConfig config;
    config.type = FaultType::MessageCorrupt;
    config.probability = probability;
    config.target_stream = StreamType::Delta;
    return config;
}

/// @brief Create a fault config that simulates a network partition to a node
///
/// @param target The node to partition
/// @return Configured FaultConfig
[[nodiscard]] inline FaultConfig partition_node(const NodeId& target) {
    FaultConfig config;
    config.type = FaultType::PartialPartition;
    config.probability = 1.0;
    config.target_node = target;
    return config;
}

/// @brief Create a fault config that simulates a slow node
///
/// @param target The node to slow down (messages TO this node are delayed)
/// @param delay The processing delay to simulate
/// @return Configured FaultConfig
[[nodiscard]] inline FaultConfig slow_node(
    const NodeId& target,
    std::chrono::milliseconds delay) {

    FaultConfig config;
    config.type = FaultType::SlowNode;
    config.probability = 1.0;
    config.target_node = target;
    config.delay = delay;
    return config;
}

/// @brief Create a fault config that drops Raft messages with probability
///
/// Useful for testing leader election under message loss.
///
/// @param probability Drop probability (0.0 - 1.0)
/// @return Configured FaultConfig
[[nodiscard]] inline FaultConfig drop_raft_messages(double probability) {
    FaultConfig config;
    config.type = FaultType::MessageDrop;
    config.probability = probability;
    config.target_stream = StreamType::Raft;
    return config;
}

}  // namespace dotvm::core::state::replication::testing
