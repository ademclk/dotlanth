#pragma once

/// @file message_types.hpp
/// @brief STATE-006 Native C++ message types for replication
///
/// Defines message types for the replication protocol:
/// - Raft consensus messages (RequestVote, AppendEntries)
/// - Delta streaming messages (DeltaBatch, DeltaAck)
/// - Snapshot transfer messages (SnapshotChunk, SnapshotAck)
/// - Control messages (Heartbeat, ConfigChange)

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "dotvm/core/state/log_record.hpp"
#include "dotvm/core/state/mpt_types.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Type Aliases
// ============================================================================

/// @brief MPT root hash type (32-byte hash)
using MptHash = ::dotvm::core::state::Hash256;

// ============================================================================
// Node Identity
// ============================================================================

/// @brief Unique identifier for a node in the cluster
///
/// NodeId is a 16-byte (128-bit) identifier, typically a UUID or hash.
/// The format allows for both human-readable and collision-resistant IDs.
struct NodeId {
    std::array<std::uint8_t, 16> data{};

    [[nodiscard]] bool operator==(const NodeId&) const noexcept = default;
    [[nodiscard]] bool operator<(const NodeId& other) const noexcept {
        return data < other.data;
    }

    /// @brief Check if this is the null/invalid node ID
    [[nodiscard]] bool is_null() const noexcept {
        for (auto b : data) {
            if (b != 0) return false;
        }
        return true;
    }

    /// @brief Create a null node ID
    [[nodiscard]] static constexpr NodeId null() noexcept { return NodeId{}; }

    /// @brief Convert to hex string representation
    [[nodiscard]] std::string to_hex() const;

    /// @brief Parse from hex string
    [[nodiscard]] static std::optional<NodeId> from_hex(std::string_view hex);
};

// ============================================================================
// Raft Terms and Indices
// ============================================================================

/// @brief Raft term number (election epoch)
struct Term {
    std::uint64_t value{0};

    [[nodiscard]] constexpr bool operator==(const Term&) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const Term& other) const noexcept {
        return value < other.value;
    }
    [[nodiscard]] constexpr bool operator<=(const Term& other) const noexcept {
        return value <= other.value;
    }
    [[nodiscard]] constexpr bool operator>(const Term& other) const noexcept {
        return value > other.value;
    }
    [[nodiscard]] constexpr bool operator>=(const Term& other) const noexcept {
        return value >= other.value;
    }

    [[nodiscard]] constexpr Term next() const noexcept { return Term{value + 1}; }
};

/// @brief Log index in Raft log (1-indexed, 0 is invalid)
struct LogIndex {
    std::uint64_t value{0};

    [[nodiscard]] static constexpr LogIndex invalid() noexcept { return LogIndex{0}; }
    [[nodiscard]] static constexpr LogIndex first() noexcept { return LogIndex{1}; }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return value > 0; }
    [[nodiscard]] constexpr LogIndex next() const noexcept { return LogIndex{value + 1}; }
    [[nodiscard]] constexpr LogIndex prev() const noexcept {
        return LogIndex{value > 0 ? value - 1 : 0};
    }

    [[nodiscard]] constexpr bool operator==(const LogIndex&) const noexcept = default;
    [[nodiscard]] constexpr bool operator<(const LogIndex& other) const noexcept {
        return value < other.value;
    }
    [[nodiscard]] constexpr bool operator<=(const LogIndex& other) const noexcept {
        return value <= other.value;
    }
    [[nodiscard]] constexpr bool operator>(const LogIndex& other) const noexcept {
        return value > other.value;
    }
    [[nodiscard]] constexpr bool operator>=(const LogIndex& other) const noexcept {
        return value >= other.value;
    }
};

// ============================================================================
// Raft Command Types (Metadata Plane Only)
// ============================================================================

/// @brief Type of command in Raft log (metadata operations only)
enum class RaftCommandType : std::uint8_t {
    AddNode = 0,          ///< Add a node to cluster membership
    RemoveNode = 1,       ///< Remove a node from cluster membership
    SnapshotMarker = 2,   ///< Mark a snapshot LSN for truncation
    ConfigChange = 3,     ///< Cluster configuration change
    Noop = 4,             ///< No-operation (used for log consistency)
};

/// @brief A command entry in the Raft log
struct RaftCommand {
    RaftCommandType type{RaftCommandType::Noop};
    std::vector<std::byte> data;  ///< Command-specific payload
};

// ============================================================================
// Raft Log Entry
// ============================================================================

/// @brief A single entry in the Raft log
struct RaftLogEntry {
    Term term;              ///< Term when entry was created
    LogIndex index;         ///< Position in log (1-indexed)
    RaftCommand command;    ///< The command to apply
};

// ============================================================================
// Raft Messages
// ============================================================================

/// @brief RequestVote RPC request
struct RequestVote {
    Term term;              ///< Candidate's term
    NodeId candidate_id;    ///< Candidate requesting vote
    LogIndex last_log_index;  ///< Index of candidate's last log entry
    Term last_log_term;     ///< Term of candidate's last log entry
};

/// @brief RequestVote RPC response
struct RequestVoteResponse {
    Term term;              ///< Current term, for candidate to update itself
    bool vote_granted{false};  ///< True if candidate received vote
};

/// @brief AppendEntries RPC request (also used for heartbeat when entries is empty)
struct AppendEntries {
    Term term;              ///< Leader's term
    NodeId leader_id;       ///< Leader's ID so followers can redirect clients
    LogIndex prev_log_index;  ///< Index of log entry immediately preceding new ones
    Term prev_log_term;     ///< Term of prev_log_index entry
    std::vector<RaftLogEntry> entries;  ///< Log entries to store (empty for heartbeat)
    LogIndex leader_commit;  ///< Leader's commit index
};

/// @brief AppendEntries RPC response
struct AppendEntriesResponse {
    Term term;              ///< Current term, for leader to update itself
    bool success{false};    ///< True if follower matched prev_log_index and term
    LogIndex match_index;   ///< Follower's last replicated index (for leader tracking)
};

/// @brief InstallSnapshot RPC request (for lagging followers)
struct InstallSnapshot {
    Term term;              ///< Leader's term
    NodeId leader_id;       ///< Leader's ID
    LogIndex last_included_index;  ///< Last log index in snapshot
    Term last_included_term;  ///< Term of last_included_index
    std::uint64_t offset;   ///< Byte offset where chunk is positioned
    std::vector<std::byte> data;  ///< Raw snapshot chunk data
    bool done{false};       ///< True if this is the last chunk
};

/// @brief InstallSnapshot RPC response
struct InstallSnapshotResponse {
    Term term;              ///< Current term, for leader to update itself
};

// ============================================================================
// Delta Streaming Messages (Data Plane)
// ============================================================================

/// @brief A batch of WAL entries for delta streaming
struct DeltaBatch {
    NodeId sender_id;       ///< Node that sent this batch
    LSN start_lsn;          ///< LSN of first entry in batch
    LSN end_lsn;            ///< LSN of last entry in batch (inclusive)
    LSN base_lsn;           ///< Base LSN (LSN before first entry, for ordering)
    std::vector<LogRecord> entries;  ///< The log records
    std::uint32_t checksum{0};  ///< CRC32 of serialized entries
    std::optional<MptHash> mpt_root_after;  ///< Expected MPT root after applying batch
};

/// @brief Acknowledgment for received delta batch
struct DeltaAck {
    NodeId node_id;         ///< Node sending the ack
    LSN acked_lsn;          ///< Highest LSN successfully applied
    MptHash mpt_root;       ///< MPT root after applying batch
    bool success{true};     ///< False if apply failed
    std::string error_msg;  ///< Error message if success=false
};

/// @brief Request for delta entries starting from a specific LSN
struct DeltaRequest {
    NodeId node_id;         ///< Node requesting deltas
    LSN start_lsn;          ///< Starting LSN (exclusive)
    std::uint32_t max_entries{1000};  ///< Max entries to return
    std::uint32_t max_bytes{64 * 1024};  ///< Max bytes to return
};

/// @brief Request retransmission of delta entries from a specific LSN
struct RetransmitRequest {
    NodeId node_id;         ///< Node requesting retransmission
    LSN from_lsn;           ///< Starting LSN for retransmission
};

// ============================================================================
// Snapshot Transfer Messages
// ============================================================================

/// @brief A chunk of snapshot data for bulk transfer
struct SnapshotChunk {
    NodeId sender_id;       ///< Node sending the snapshot
    LSN snapshot_lsn;       ///< LSN at which snapshot was taken
    std::uint32_t chunk_index;  ///< Zero-based chunk index
    std::uint32_t total_chunks;  ///< Total number of chunks (0 if unknown)
    std::uint64_t total_bytes;  ///< Total snapshot size in bytes (0 if unknown)
    std::vector<std::byte> data;  ///< Chunk payload
    std::uint32_t checksum{0};  ///< CRC32 of chunk data
    bool is_last{false};    ///< True if this is the final chunk
};

/// @brief Acknowledgment for received snapshot chunk
struct SnapshotAck {
    NodeId node_id;         ///< Node sending the ack
    std::uint32_t chunk_index;  ///< Index of acknowledged chunk
    bool success{true};     ///< False if chunk was rejected
    std::string error_msg;  ///< Error message if success=false
};

/// @brief Request to start snapshot transfer
struct SnapshotRequest {
    NodeId node_id;         ///< Node requesting snapshot
    LSN from_lsn;           ///< Last known LSN (for gap detection)
};

// ============================================================================
// Control Messages
// ============================================================================

/// @brief Heartbeat message for connection keep-alive
struct Heartbeat {
    NodeId sender_id;       ///< Node sending heartbeat
    std::uint64_t timestamp_us;  ///< Microseconds since epoch
    LSN current_lsn;        ///< Sender's current LSN
    bool is_leader{false};  ///< True if sender is cluster leader
};

/// @brief Response to heartbeat
struct HeartbeatResponse {
    NodeId node_id;         ///< Node responding
    std::uint64_t timestamp_us;  ///< Response timestamp
    LSN current_lsn;        ///< Responder's current LSN
};

/// @brief Cluster membership configuration
struct ClusterConfig {
    std::uint64_t config_index;  ///< Configuration version
    std::vector<NodeId> members;  ///< Current cluster members
    std::vector<NodeId> learners;  ///< Non-voting learner nodes
};

/// @brief Request to change cluster membership
struct ConfigChangeRequest {
    NodeId requester_id;    ///< Node requesting change
    RaftCommandType change_type;  ///< AddNode or RemoveNode
    NodeId target_node;     ///< Node being added/removed
    std::string address;    ///< Network address for new node
};

// ============================================================================
// Unified Message Type
// ============================================================================

/// @brief Message type tag for routing
enum class MessageType : std::uint8_t {
    // Raft messages (0-15)
    RequestVote = 0,
    RequestVoteResponse = 1,
    AppendEntries = 2,
    AppendEntriesResponse = 3,
    InstallSnapshot = 4,
    InstallSnapshotResponse = 5,

    // Delta messages (16-31)
    DeltaBatch = 16,
    DeltaAck = 17,
    DeltaRequest = 18,

    // Snapshot messages (32-47)
    SnapshotChunk = 32,
    SnapshotAck = 33,
    SnapshotRequest = 34,

    // Control messages (48-63)
    Heartbeat = 48,
    HeartbeatResponse = 49,
    ClusterConfig = 50,
    ConfigChangeRequest = 51,
};

/// @brief Convert message type to string for debugging
[[nodiscard]] constexpr std::string_view to_string(MessageType type) noexcept {
    switch (type) {
        case MessageType::RequestVote:
            return "RequestVote";
        case MessageType::RequestVoteResponse:
            return "RequestVoteResponse";
        case MessageType::AppendEntries:
            return "AppendEntries";
        case MessageType::AppendEntriesResponse:
            return "AppendEntriesResponse";
        case MessageType::InstallSnapshot:
            return "InstallSnapshot";
        case MessageType::InstallSnapshotResponse:
            return "InstallSnapshotResponse";
        case MessageType::DeltaBatch:
            return "DeltaBatch";
        case MessageType::DeltaAck:
            return "DeltaAck";
        case MessageType::DeltaRequest:
            return "DeltaRequest";
        case MessageType::SnapshotChunk:
            return "SnapshotChunk";
        case MessageType::SnapshotAck:
            return "SnapshotAck";
        case MessageType::SnapshotRequest:
            return "SnapshotRequest";
        case MessageType::Heartbeat:
            return "Heartbeat";
        case MessageType::HeartbeatResponse:
            return "HeartbeatResponse";
        case MessageType::ClusterConfig:
            return "ClusterConfig";
        case MessageType::ConfigChangeRequest:
            return "ConfigChangeRequest";
    }
    return "Unknown";
}

/// @brief Variant holding any replication message
using ReplicationMessage = std::variant<
    RequestVote,
    RequestVoteResponse,
    AppendEntries,
    AppendEntriesResponse,
    InstallSnapshot,
    InstallSnapshotResponse,
    DeltaBatch,
    DeltaAck,
    DeltaRequest,
    SnapshotChunk,
    SnapshotAck,
    SnapshotRequest,
    Heartbeat,
    HeartbeatResponse,
    ClusterConfig,
    ConfigChangeRequest>;

/// @brief Get the MessageType for a message variant
[[nodiscard]] constexpr MessageType get_message_type(const ReplicationMessage& msg) noexcept {
    return std::visit(
        []<typename T>(const T&) -> MessageType {
            if constexpr (std::same_as<T, RequestVote>)
                return MessageType::RequestVote;
            else if constexpr (std::same_as<T, RequestVoteResponse>)
                return MessageType::RequestVoteResponse;
            else if constexpr (std::same_as<T, AppendEntries>)
                return MessageType::AppendEntries;
            else if constexpr (std::same_as<T, AppendEntriesResponse>)
                return MessageType::AppendEntriesResponse;
            else if constexpr (std::same_as<T, InstallSnapshot>)
                return MessageType::InstallSnapshot;
            else if constexpr (std::same_as<T, InstallSnapshotResponse>)
                return MessageType::InstallSnapshotResponse;
            else if constexpr (std::same_as<T, DeltaBatch>)
                return MessageType::DeltaBatch;
            else if constexpr (std::same_as<T, DeltaAck>)
                return MessageType::DeltaAck;
            else if constexpr (std::same_as<T, DeltaRequest>)
                return MessageType::DeltaRequest;
            else if constexpr (std::same_as<T, SnapshotChunk>)
                return MessageType::SnapshotChunk;
            else if constexpr (std::same_as<T, SnapshotAck>)
                return MessageType::SnapshotAck;
            else if constexpr (std::same_as<T, SnapshotRequest>)
                return MessageType::SnapshotRequest;
            else if constexpr (std::same_as<T, Heartbeat>)
                return MessageType::Heartbeat;
            else if constexpr (std::same_as<T, HeartbeatResponse>)
                return MessageType::HeartbeatResponse;
            else if constexpr (std::same_as<T, ClusterConfig>)
                return MessageType::ClusterConfig;
            else if constexpr (std::same_as<T, ConfigChangeRequest>)
                return MessageType::ConfigChangeRequest;
            else
                return MessageType::Heartbeat;  // Fallback
        },
        msg);
}

}  // namespace dotvm::core::state::replication
