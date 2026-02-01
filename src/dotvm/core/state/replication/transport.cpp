/// @file transport.cpp
/// @brief Implementation of MockTransport for testing

#include "dotvm/core/state/replication/transport.hpp"

#include <map>
#include <mutex>
#include <random>
#include <set>
#include <thread>

namespace dotvm::core::state::replication {

// ============================================================================
// MockTransport Implementation
// ============================================================================

struct MockTransport::Impl {
    TransportConfig config;
    NodeId local_id;
    bool running{false};

    mutable std::mutex mtx;
    MessageCallback message_callback;
    ConnectionCallback connection_callback;

    // Linked transports
    std::map<NodeId, MockTransport*> linked_transports;

    // Connection states
    std::map<NodeId, ConnectionState> connection_states;
    std::map<NodeId, ConnectionStats> connection_stats;

    // Partitioned nodes (dropped messages)
    std::set<NodeId> partitioned_nodes;

    // Fault injection
    std::chrono::milliseconds latency{0};
    double drop_rate{0.0};
    std::mt19937 rng{42};

    // Statistics
    std::uint64_t total_sent{0};
    std::uint64_t total_received{0};

    explicit Impl(TransportConfig cfg) : config(std::move(cfg)) {}
};

MockTransport::MockTransport(TransportConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

MockTransport::~MockTransport() {
    if (impl_->running) {
        stop(std::chrono::milliseconds{0});
    }
}

MockTransport::Result<void> MockTransport::start(NodeId local_id) {
    std::lock_guard lock(impl_->mtx);
    if (impl_->running) {
        return ReplicationError::InvalidClusterConfig;
    }
    impl_->local_id = local_id;
    impl_->running = true;
    return {};
}

void MockTransport::stop(std::chrono::milliseconds /*timeout*/) {
    std::lock_guard lock(impl_->mtx);
    impl_->running = false;

    // Notify all connections are closing
    for (auto& [peer, state] : impl_->connection_states) {
        if (state == ConnectionState::Connected) {
            auto old_state = state;
            state = ConnectionState::Disconnected;
            if (impl_->connection_callback) {
                impl_->connection_callback(peer, old_state, ConnectionState::Disconnected,
                                           std::nullopt);
            }
        }
    }
    impl_->connection_states.clear();
    impl_->connection_stats.clear();
}

bool MockTransport::is_running() const noexcept {
    std::lock_guard lock(impl_->mtx);
    return impl_->running;
}

MockTransport::Result<void> MockTransport::connect(const NodeId& peer,
                                                    std::string_view /*address*/) {
    std::lock_guard lock(impl_->mtx);
    if (!impl_->running) {
        return ReplicationError::ShuttingDown;
    }

    auto it = impl_->linked_transports.find(peer);
    if (it == impl_->linked_transports.end()) {
        // No linked transport - fail connection
        // Capture old state BEFORE modifying
        auto state_it = impl_->connection_states.find(peer);
        auto old_state = (state_it != impl_->connection_states.end()) ? state_it->second
                                                                      : ConnectionState::Disconnected;
        impl_->connection_states[peer] = ConnectionState::Failed;
        if (impl_->connection_callback) {
            impl_->connection_callback(peer, old_state, ConnectionState::Failed,
                                       ReplicationError::ConnectionFailed);
        }
        return ReplicationError::ConnectionFailed;
    }

    // Simulate successful connection
    // Capture old state BEFORE modifying
    auto state_it = impl_->connection_states.find(peer);
    auto old_state = (state_it != impl_->connection_states.end()) ? state_it->second
                                                                  : ConnectionState::Disconnected;
    impl_->connection_states[peer] = ConnectionState::Connected;

    ConnectionStats stats;
    stats.connected_at = std::chrono::steady_clock::now();
    stats.last_activity = stats.connected_at;
    impl_->connection_stats[peer] = stats;

    if (impl_->connection_callback) {
        impl_->connection_callback(peer, old_state, ConnectionState::Connected, std::nullopt);
    }

    return {};
}

MockTransport::Result<void> MockTransport::disconnect(const NodeId& peer, bool /*graceful*/) {
    std::lock_guard lock(impl_->mtx);

    auto it = impl_->connection_states.find(peer);
    if (it == impl_->connection_states.end() || it->second == ConnectionState::Disconnected) {
        return ReplicationError::NodeNotFound;
    }

    auto old_state = it->second;
    it->second = ConnectionState::Disconnected;

    if (impl_->connection_callback) {
        impl_->connection_callback(peer, old_state, ConnectionState::Disconnected, std::nullopt);
    }

    return {};
}

ConnectionState MockTransport::get_state(const NodeId& peer) const noexcept {
    std::lock_guard lock(impl_->mtx);
    auto it = impl_->connection_states.find(peer);
    if (it == impl_->connection_states.end()) {
        return ConnectionState::Disconnected;
    }
    return it->second;
}

std::optional<ConnectionStats> MockTransport::get_stats(const NodeId& peer) const noexcept {
    std::lock_guard lock(impl_->mtx);
    auto it = impl_->connection_stats.find(peer);
    if (it == impl_->connection_stats.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<NodeId> MockTransport::connected_peers() const {
    std::lock_guard lock(impl_->mtx);
    std::vector<NodeId> result;
    for (const auto& [peer, state] : impl_->connection_states) {
        if (state == ConnectionState::Connected) {
            result.push_back(peer);
        }
    }
    return result;
}

MockTransport::Result<void> MockTransport::send(const NodeId& to, StreamType stream,
                                                 std::span<const std::byte> data) {
    MockTransport* target = nullptr;
    bool partitioned = false;

    {
        std::lock_guard lock(impl_->mtx);
        if (!impl_->running) {
            return ReplicationError::ShuttingDown;
        }

        auto state_it = impl_->connection_states.find(to);
        if (state_it == impl_->connection_states.end() ||
            state_it->second != ConnectionState::Connected) {
            return ReplicationError::ConnectionClosed;
        }

        partitioned = impl_->partitioned_nodes.contains(to);

        auto link_it = impl_->linked_transports.find(to);
        if (link_it != impl_->linked_transports.end()) {
            target = link_it->second;
        }

        // Update stats
        if (auto stats_it = impl_->connection_stats.find(to);
            stats_it != impl_->connection_stats.end()) {
            stats_it->second.bytes_sent += data.size();
            stats_it->second.messages_sent++;
            stats_it->second.last_activity = std::chrono::steady_clock::now();
        }

        impl_->total_sent++;
    }

    // Check for partition
    if (partitioned) {
        return {};  // Silently drop
    }

    // Check for random drop
    if (impl_->drop_rate > 0.0) {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        std::lock_guard lock(impl_->mtx);
        if (dist(impl_->rng) < impl_->drop_rate) {
            return {};  // Drop this message
        }
    }

    if (target == nullptr) {
        return ReplicationError::NodeNotFound;
    }

    // Copy data for delivery
    std::vector<std::byte> data_copy(data.begin(), data.end());

    // Simulate latency
    if (impl_->latency.count() > 0) {
        std::this_thread::sleep_for(impl_->latency);
    }

    // Deliver to target
    target->deliver_message(impl_->local_id, stream, std::move(data_copy));

    return {};
}

std::size_t MockTransport::broadcast(StreamType stream, std::span<const std::byte> data,
                                      std::optional<NodeId> exclude) {
    std::vector<NodeId> peers;
    {
        std::lock_guard lock(impl_->mtx);
        for (const auto& [peer, state] : impl_->connection_states) {
            if (state == ConnectionState::Connected &&
                (!exclude.has_value() || peer != exclude.value())) {
                peers.push_back(peer);
            }
        }
    }

    std::size_t count = 0;
    for (const auto& peer : peers) {
        if (send(peer, stream, data).is_ok()) {
            ++count;
        }
    }
    return count;
}

void MockTransport::set_message_callback(MessageCallback callback) {
    std::lock_guard lock(impl_->mtx);
    impl_->message_callback = std::move(callback);
}

void MockTransport::set_connection_callback(ConnectionCallback callback) {
    std::lock_guard lock(impl_->mtx);
    impl_->connection_callback = std::move(callback);
}

const TransportConfig& MockTransport::config() const noexcept { return impl_->config; }

NodeId MockTransport::local_id() const noexcept {
    std::lock_guard lock(impl_->mtx);
    return impl_->local_id;
}

// ============================================================================
// Mock-Specific Methods
// ============================================================================

void MockTransport::link_to(MockTransport& other) {
    // Link both directions
    {
        std::lock_guard lock(impl_->mtx);
        std::lock_guard other_lock(other.impl_->mtx);

        // Get other's local_id
        NodeId other_id = other.impl_->local_id;
        NodeId my_id = impl_->local_id;

        impl_->linked_transports[other_id] = &other;
        other.impl_->linked_transports[my_id] = this;
    }
}

void MockTransport::unlink_from(const NodeId& peer) {
    std::lock_guard lock(impl_->mtx);
    auto it = impl_->linked_transports.find(peer);
    if (it != impl_->linked_transports.end()) {
        MockTransport* other = it->second;
        impl_->linked_transports.erase(it);

        // Also remove from other side
        std::lock_guard other_lock(other->impl_->mtx);
        other->impl_->linked_transports.erase(impl_->local_id);
    }
}

void MockTransport::partition_from(const NodeId& peer) {
    std::lock_guard lock(impl_->mtx);
    impl_->partitioned_nodes.insert(peer);
}

void MockTransport::heal_partition(const NodeId& peer) {
    std::lock_guard lock(impl_->mtx);
    impl_->partitioned_nodes.erase(peer);
}

void MockTransport::set_latency(std::chrono::milliseconds latency) {
    std::lock_guard lock(impl_->mtx);
    impl_->latency = latency;
}

void MockTransport::set_drop_rate(double rate) {
    std::lock_guard lock(impl_->mtx);
    impl_->drop_rate = rate;
}

std::uint64_t MockTransport::messages_sent() const noexcept {
    std::lock_guard lock(impl_->mtx);
    return impl_->total_sent;
}

std::uint64_t MockTransport::messages_received() const noexcept {
    std::lock_guard lock(impl_->mtx);
    return impl_->total_received;
}

void MockTransport::deliver_message(const NodeId& from, StreamType stream,
                                     std::vector<std::byte> data) {
    MessageCallback callback;
    {
        std::lock_guard lock(impl_->mtx);

        // Check if partitioned
        if (impl_->partitioned_nodes.contains(from)) {
            return;  // Silently drop
        }

        impl_->total_received++;

        // Update stats
        if (auto stats_it = impl_->connection_stats.find(from);
            stats_it != impl_->connection_stats.end()) {
            stats_it->second.bytes_received += data.size();
            stats_it->second.messages_received++;
            stats_it->second.last_activity = std::chrono::steady_clock::now();
        }

        callback = impl_->message_callback;
    }

    // Invoke callback outside lock
    if (callback) {
        callback(from, stream, data);
    }
}

}  // namespace dotvm::core::state::replication
