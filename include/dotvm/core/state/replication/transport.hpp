#pragma once

/// @file transport.hpp
/// @brief STATE-006 Abstract transport interface for replication
///
/// Defines the abstract Transport interface and related types for
/// replication communication. The interface is designed to support
/// QUIC transport with stream multiplexing.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Stream Types
// ============================================================================

/// @brief Type of stream for multiplexing
///
/// Different message types use different streams for priority and
/// flow control separation.
enum class StreamType : std::uint8_t {
    Raft = 0,      ///< High priority: Consensus messages (RequestVote, AppendEntries)
    Delta = 1,     ///< Medium priority: WAL delta streaming
    Snapshot = 2,  ///< Low priority: Bulk snapshot transfer
    Control = 3,   ///< High priority: Heartbeats, configuration
};

/// @brief Convert stream type to string
[[nodiscard]] constexpr std::string_view to_string(StreamType type) noexcept {
    switch (type) {
        case StreamType::Raft:
            return "Raft";
        case StreamType::Delta:
            return "Delta";
        case StreamType::Snapshot:
            return "Snapshot";
        case StreamType::Control:
            return "Control";
    }
    return "Unknown";
}

/// @brief Get the appropriate stream type for a message
[[nodiscard]] constexpr StreamType get_stream_type(MessageType msg_type) noexcept {
    switch (msg_type) {
        case MessageType::RequestVote:
        case MessageType::RequestVoteResponse:
        case MessageType::AppendEntries:
        case MessageType::AppendEntriesResponse:
        case MessageType::InstallSnapshot:
        case MessageType::InstallSnapshotResponse:
            return StreamType::Raft;

        case MessageType::DeltaBatch:
        case MessageType::DeltaAck:
        case MessageType::DeltaRequest:
            return StreamType::Delta;

        case MessageType::SnapshotChunk:
        case MessageType::SnapshotAck:
        case MessageType::SnapshotRequest:
            return StreamType::Snapshot;

        case MessageType::Heartbeat:
        case MessageType::HeartbeatResponse:
        case MessageType::ClusterConfig:
        case MessageType::ConfigChangeRequest:
            return StreamType::Control;
    }
    return StreamType::Control;
}

// ============================================================================
// Connection State
// ============================================================================

/// @brief State of a transport connection
enum class ConnectionState : std::uint8_t {
    Disconnected = 0,  ///< Not connected
    Connecting = 1,    ///< Connection in progress
    Connected = 2,     ///< Connected and ready
    Draining = 3,      ///< Graceful shutdown in progress
    Failed = 4,        ///< Connection failed (error occurred)
};

/// @brief Convert connection state to string
[[nodiscard]] constexpr std::string_view to_string(ConnectionState state) noexcept {
    switch (state) {
        case ConnectionState::Disconnected:
            return "Disconnected";
        case ConnectionState::Connecting:
            return "Connecting";
        case ConnectionState::Connected:
            return "Connected";
        case ConnectionState::Draining:
            return "Draining";
        case ConnectionState::Failed:
            return "Failed";
    }
    return "Unknown";
}

// ============================================================================
// Connection Statistics
// ============================================================================

/// @brief Statistics for a connection
struct ConnectionStats {
    std::uint64_t bytes_sent{0};
    std::uint64_t bytes_received{0};
    std::uint64_t messages_sent{0};
    std::uint64_t messages_received{0};
    std::uint64_t errors{0};
    std::chrono::steady_clock::time_point connected_at;
    std::chrono::steady_clock::time_point last_activity;
    std::chrono::microseconds rtt_estimate{0};  ///< Round-trip time estimate
};

// ============================================================================
// Transport Configuration
// ============================================================================

/// @brief Configuration for transport layer
struct TransportConfig {
    std::string bind_address{"0.0.0.0"};  ///< Address to bind to
    std::uint16_t bind_port{4000};        ///< Port to listen on

    // Timeouts
    std::chrono::milliseconds connect_timeout{5000};     ///< Connection timeout
    std::chrono::milliseconds idle_timeout{30000};       ///< Idle connection timeout
    std::chrono::milliseconds heartbeat_interval{1000};  ///< Heartbeat interval

    // Buffer sizes
    std::size_t send_buffer_size{256 * 1024};     ///< Per-connection send buffer
    std::size_t receive_buffer_size{256 * 1024};  ///< Per-connection receive buffer

    // TLS configuration (for QUIC)
    std::string cert_path;  ///< Path to TLS certificate
    std::string key_path;   ///< Path to TLS private key
    std::string ca_path;    ///< Path to CA certificate (for verification)

    // Flow control
    std::size_t max_pending_sends{1000};  ///< Max messages queued for send
    bool enable_backpressure{true};       ///< Enable send backpressure

    /// @brief Create default configuration
    [[nodiscard]] static TransportConfig defaults() noexcept { return TransportConfig{}; }
};

// ============================================================================
// Callbacks
// ============================================================================

/// @brief Callback for incoming messages
///
/// @param from Source node ID
/// @param stream Stream the message arrived on
/// @param data Raw message bytes (including header)
using MessageCallback =
    std::function<void(const NodeId& from, StreamType stream, std::span<const std::byte> data)>;

/// @brief Callback for connection state changes
///
/// @param peer Peer node ID
/// @param old_state Previous state
/// @param new_state New state
/// @param error Error code if state is Failed
using ConnectionCallback =
    std::function<void(const NodeId& peer, ConnectionState old_state, ConnectionState new_state,
                       std::optional<ReplicationError> error)>;

// ============================================================================
// Transport Interface
// ============================================================================

/// @brief Abstract interface for replication transport
///
/// Transport provides bidirectional message passing between cluster nodes.
/// The interface is designed to support stream multiplexing (like QUIC) but
/// can be implemented with simpler transports for testing.
///
/// Thread Safety: Implementations MUST be thread-safe. Multiple threads
/// may call send() and callbacks may be invoked from different threads.
///
/// @par Design Notes
/// - Messages are sent as opaque byte spans (already serialized)
/// - The transport handles connection management and reconnection
/// - Stream types allow priority-based scheduling
/// - Backpressure is signaled via BackpressureExceeded error
class Transport {
public:
    virtual ~Transport() = default;

    // Non-copyable, non-movable
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

protected:
    Transport() = default;

public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Start the transport layer
    ///
    /// Begins listening for incoming connections and allows outgoing
    /// connections to be established.
    ///
    /// @param local_id This node's ID
    /// @return Success or error
    [[nodiscard]] virtual Result<void> start(NodeId local_id) = 0;

    /// @brief Stop the transport layer
    ///
    /// Gracefully shuts down all connections and stops listening.
    /// Blocks until shutdown is complete or timeout is reached.
    ///
    /// @param timeout Maximum time to wait for graceful shutdown
    virtual void stop(std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) = 0;

    /// @brief Check if transport is running
    [[nodiscard]] virtual bool is_running() const noexcept = 0;

    // ========================================================================
    // Connection Management
    // ========================================================================

    /// @brief Connect to a peer node
    ///
    /// Initiates an outgoing connection. The connection may complete
    /// asynchronously; use the connection callback to monitor state.
    ///
    /// @param peer Peer node ID
    /// @param address Network address (e.g., "192.168.1.1:4000")
    /// @return Success if connection initiated, or error
    [[nodiscard]] virtual Result<void> connect(const NodeId& peer, std::string_view address) = 0;

    /// @brief Disconnect from a peer
    ///
    /// @param peer Peer node ID
    /// @param graceful If true, drain pending messages before closing
    [[nodiscard]] virtual Result<void> disconnect(const NodeId& peer, bool graceful = true) = 0;

    /// @brief Get current connection state for a peer
    [[nodiscard]] virtual ConnectionState get_state(const NodeId& peer) const noexcept = 0;

    /// @brief Get connection statistics for a peer
    [[nodiscard]] virtual std::optional<ConnectionStats>
    get_stats(const NodeId& peer) const noexcept = 0;

    /// @brief Get list of connected peers
    [[nodiscard]] virtual std::vector<NodeId> connected_peers() const = 0;

    // ========================================================================
    // Message Sending
    // ========================================================================

    /// @brief Send a message to a peer
    ///
    /// The message is sent on the appropriate stream based on its type.
    /// If the send buffer is full and backpressure is enabled, returns
    /// BackpressureExceeded.
    ///
    /// @param to Destination node ID
    /// @param stream Stream to send on
    /// @param data Message bytes (must include header)
    /// @return Success, or error if send failed
    [[nodiscard]] virtual Result<void> send(const NodeId& to, StreamType stream,
                                            std::span<const std::byte> data) = 0;

    /// @brief Broadcast a message to all connected peers
    ///
    /// @param stream Stream to send on
    /// @param data Message bytes
    /// @param exclude Optional node to exclude from broadcast
    /// @return Number of peers the message was queued for
    [[nodiscard]] virtual std::size_t broadcast(StreamType stream, std::span<const std::byte> data,
                                                std::optional<NodeId> exclude = std::nullopt) = 0;

    // ========================================================================
    // Callbacks
    // ========================================================================

    /// @brief Set callback for incoming messages
    virtual void set_message_callback(MessageCallback callback) = 0;

    /// @brief Set callback for connection state changes
    virtual void set_connection_callback(ConnectionCallback callback) = 0;

    // ========================================================================
    // Configuration
    // ========================================================================

    /// @brief Get the transport configuration
    [[nodiscard]] virtual const TransportConfig& config() const noexcept = 0;

    /// @brief Get this node's ID
    [[nodiscard]] virtual NodeId local_id() const noexcept = 0;
};

// ============================================================================
// Mock Transport (for testing)
// ============================================================================

/// @brief Mock transport implementation for testing
///
/// MockTransport provides an in-memory transport that can be linked with
/// other MockTransport instances for simulating a cluster. Useful for
/// unit testing without network I/O.
class MockTransport : public Transport {
public:
    /// @brief Create a mock transport with configuration
    explicit MockTransport(TransportConfig config = TransportConfig::defaults());

    ~MockTransport() override;

    // ========================================================================
    // Transport Interface
    // ========================================================================

    [[nodiscard]] Result<void> start(NodeId local_id) override;
    void stop(std::chrono::milliseconds timeout) override;
    [[nodiscard]] bool is_running() const noexcept override;

    [[nodiscard]] Result<void> connect(const NodeId& peer, std::string_view address) override;
    [[nodiscard]] Result<void> disconnect(const NodeId& peer, bool graceful) override;
    [[nodiscard]] ConnectionState get_state(const NodeId& peer) const noexcept override;
    [[nodiscard]] std::optional<ConnectionStats>
    get_stats(const NodeId& peer) const noexcept override;
    [[nodiscard]] std::vector<NodeId> connected_peers() const override;

    [[nodiscard]] Result<void> send(const NodeId& to, StreamType stream,
                                    std::span<const std::byte> data) override;
    [[nodiscard]] std::size_t broadcast(StreamType stream, std::span<const std::byte> data,
                                        std::optional<NodeId> exclude = std::nullopt) override;

    void set_message_callback(MessageCallback callback) override;
    void set_connection_callback(ConnectionCallback callback) override;

    [[nodiscard]] const TransportConfig& config() const noexcept override;
    [[nodiscard]] NodeId local_id() const noexcept override;

    // ========================================================================
    // Mock-Specific Methods
    // ========================================================================

    /// @brief Link this transport to another for bidirectional communication
    ///
    /// After linking, messages sent to the peer's NodeId will be delivered
    /// to the linked transport's message callback.
    void link_to(MockTransport& other);

    /// @brief Unlink from a peer transport
    void unlink_from(const NodeId& peer);

    /// @brief Simulate a network partition (drops all messages to/from peer)
    void partition_from(const NodeId& peer);

    /// @brief Heal a network partition
    void heal_partition(const NodeId& peer);

    /// @brief Add artificial latency to message delivery
    void set_latency(std::chrono::milliseconds latency);

    /// @brief Set a message drop rate (0.0 to 1.0)
    void set_drop_rate(double rate);

    /// @brief Get number of messages sent
    [[nodiscard]] std::uint64_t messages_sent() const noexcept;

    /// @brief Get number of messages received
    [[nodiscard]] std::uint64_t messages_received() const noexcept;

    /// @brief Deliver a message (called by linked transport)
    void deliver_message(const NodeId& from, StreamType stream, std::vector<std::byte> data);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dotvm::core::state::replication
