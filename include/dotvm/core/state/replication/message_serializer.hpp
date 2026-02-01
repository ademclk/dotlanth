#pragma once

/// @file message_serializer.hpp
/// @brief STATE-006 Binary serialization for replication messages
///
/// Provides zero-copy-friendly serialization and deserialization for
/// replication protocol messages. The binary format is designed to be
/// compatible with future Cap'n Proto migration.
///
/// @par Binary Format
/// All messages share a common header:
/// ```
/// Header (16 bytes):
///   [0]     Message type (MessageType enum)
///   [1]     Version (currently 1)
///   [2-3]   Reserved (0x00)
///   [4-7]   Payload length (uint32, little-endian)
///   [8-11]  CRC32 of payload
///   [12-15] Reserved (0x00)
///
/// Payload:
///   Type-specific binary data
/// ```

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/state/replication/message_types.hpp"
#include "dotvm/core/state/replication/replication_error.hpp"

namespace dotvm::core::state::replication {

// ============================================================================
// Serialization Constants
// ============================================================================

/// @brief Message header size in bytes
inline constexpr std::size_t MESSAGE_HEADER_SIZE = 16;

/// @brief Current serialization format version
inline constexpr std::uint8_t MESSAGE_FORMAT_VERSION = 1;

/// @brief Maximum message payload size (16 MB)
inline constexpr std::size_t MAX_MESSAGE_PAYLOAD_SIZE = 16 * 1024 * 1024;

// ============================================================================
// MessageSerializer
// ============================================================================

/// @brief Serializes and deserializes replication messages
///
/// Thread Safety: Thread-safe. All methods are stateless and can be called
/// concurrently.
///
/// @par Usage
/// ```cpp
/// // Serialize
/// RequestVote vote{...};
/// auto bytes = MessageSerializer::serialize(vote);
///
/// // Deserialize
/// auto result = MessageSerializer::deserialize(bytes);
/// if (result.is_ok()) {
///     std::visit([](auto&& msg) { ... }, result.value());
/// }
/// ```
class MessageSerializer {
public:
    template <typename T>
    using Result = ::dotvm::core::Result<T, ReplicationError>;

    // ========================================================================
    // Type-Specific Serialization
    // ========================================================================

    /// @brief Serialize a RequestVote message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const RequestVote& msg);

    /// @brief Serialize a RequestVoteResponse message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const RequestVoteResponse& msg);

    /// @brief Serialize an AppendEntries message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const AppendEntries& msg);

    /// @brief Serialize an AppendEntriesResponse message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const AppendEntriesResponse& msg);

    /// @brief Serialize an InstallSnapshot message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const InstallSnapshot& msg);

    /// @brief Serialize an InstallSnapshotResponse message
    [[nodiscard]] static Result<std::vector<std::byte>>
    serialize(const InstallSnapshotResponse& msg);

    /// @brief Serialize a DeltaBatch message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const DeltaBatch& msg);

    /// @brief Serialize a DeltaAck message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const DeltaAck& msg);

    /// @brief Serialize a DeltaRequest message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const DeltaRequest& msg);

    /// @brief Serialize a SnapshotChunk message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const SnapshotChunk& msg);

    /// @brief Serialize a SnapshotAck message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const SnapshotAck& msg);

    /// @brief Serialize a SnapshotRequest message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const SnapshotRequest& msg);

    /// @brief Serialize a Heartbeat message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const Heartbeat& msg);

    /// @brief Serialize a HeartbeatResponse message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const HeartbeatResponse& msg);

    /// @brief Serialize a ClusterConfig message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const ClusterConfig& msg);

    /// @brief Serialize a ConfigChangeRequest message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const ConfigChangeRequest& msg);

    // ========================================================================
    // Generic Serialization (via variant)
    // ========================================================================

    /// @brief Serialize any replication message
    [[nodiscard]] static Result<std::vector<std::byte>> serialize(const ReplicationMessage& msg);

    // ========================================================================
    // Deserialization
    // ========================================================================

    /// @brief Deserialize a message from bytes
    ///
    /// @param data Serialized message data (including header)
    /// @return Deserialized message variant, or error
    [[nodiscard]] static Result<ReplicationMessage> deserialize(std::span<const std::byte> data);

    /// @brief Peek at the message type without full deserialization
    ///
    /// @param data At least MESSAGE_HEADER_SIZE bytes
    /// @return Message type, or error if header is invalid
    [[nodiscard]] static Result<MessageType> peek_type(std::span<const std::byte> data);

    /// @brief Get the total message size from header
    ///
    /// @param data At least MESSAGE_HEADER_SIZE bytes
    /// @return Total size (header + payload), or error if header is invalid
    [[nodiscard]] static Result<std::size_t> peek_size(std::span<const std::byte> data);

    // ========================================================================
    // Utility Functions
    // ========================================================================

    /// @brief Calculate CRC32 checksum
    [[nodiscard]] static std::uint32_t calculate_crc32(std::span<const std::byte> data);

    /// @brief Serialize a NodeId to bytes (16 bytes)
    static void serialize_node_id(const NodeId& id, std::vector<std::byte>& out);

    /// @brief Deserialize a NodeId from bytes
    [[nodiscard]] static Result<NodeId> deserialize_node_id(std::span<const std::byte> data);

    /// @brief Serialize an LSN (8 bytes, little-endian)
    static void serialize_lsn(LSN lsn, std::vector<std::byte>& out);

    /// @brief Deserialize an LSN
    [[nodiscard]] static Result<LSN> deserialize_lsn(std::span<const std::byte> data);

    /// @brief Serialize a Term (8 bytes, little-endian)
    static void serialize_term(Term term, std::vector<std::byte>& out);

    /// @brief Deserialize a Term
    [[nodiscard]] static Result<Term> deserialize_term(std::span<const std::byte> data);

    /// @brief Serialize a LogIndex (8 bytes, little-endian)
    static void serialize_log_index(LogIndex index, std::vector<std::byte>& out);

    /// @brief Deserialize a LogIndex
    [[nodiscard]] static Result<LogIndex> deserialize_log_index(std::span<const std::byte> data);

private:
    // Internal helper to build message with header
    [[nodiscard]] static std::vector<std::byte> build_message(MessageType type,
                                                              std::span<const std::byte> payload);

    // Internal deserializers for each type
    [[nodiscard]] static Result<RequestVote>
    deserialize_request_vote(std::span<const std::byte> payload);
    [[nodiscard]] static Result<RequestVoteResponse>
    deserialize_request_vote_response(std::span<const std::byte> payload);
    [[nodiscard]] static Result<AppendEntries>
    deserialize_append_entries(std::span<const std::byte> payload);
    [[nodiscard]] static Result<AppendEntriesResponse>
    deserialize_append_entries_response(std::span<const std::byte> payload);
    [[nodiscard]] static Result<InstallSnapshot>
    deserialize_install_snapshot(std::span<const std::byte> payload);
    [[nodiscard]] static Result<InstallSnapshotResponse>
    deserialize_install_snapshot_response(std::span<const std::byte> payload);
    [[nodiscard]] static Result<DeltaBatch>
    deserialize_delta_batch(std::span<const std::byte> payload);
    [[nodiscard]] static Result<DeltaAck> deserialize_delta_ack(std::span<const std::byte> payload);
    [[nodiscard]] static Result<DeltaRequest>
    deserialize_delta_request(std::span<const std::byte> payload);
    [[nodiscard]] static Result<SnapshotChunk>
    deserialize_snapshot_chunk(std::span<const std::byte> payload);
    [[nodiscard]] static Result<SnapshotAck>
    deserialize_snapshot_ack(std::span<const std::byte> payload);
    [[nodiscard]] static Result<SnapshotRequest>
    deserialize_snapshot_request(std::span<const std::byte> payload);
    [[nodiscard]] static Result<Heartbeat>
    deserialize_heartbeat(std::span<const std::byte> payload);
    [[nodiscard]] static Result<HeartbeatResponse>
    deserialize_heartbeat_response(std::span<const std::byte> payload);
    [[nodiscard]] static Result<ClusterConfig>
    deserialize_cluster_config(std::span<const std::byte> payload);
    [[nodiscard]] static Result<ConfigChangeRequest>
    deserialize_config_change_request(std::span<const std::byte> payload);
};

}  // namespace dotvm::core::state::replication
