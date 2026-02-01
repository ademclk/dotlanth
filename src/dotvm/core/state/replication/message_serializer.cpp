/// @file message_serializer.cpp
/// @brief Implementation of binary serialization for replication messages

#include "dotvm/core/state/replication/message_serializer.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace dotvm::core::state::replication {

// ============================================================================
// CRC32 Implementation (IEEE polynomial)
// ============================================================================

namespace {

// CRC32 lookup table (IEEE polynomial 0xEDB88320)
constexpr std::array<std::uint32_t, 256> generate_crc32_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t crc = i;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr auto CRC32_TABLE = generate_crc32_table();

// Little-endian helpers
void write_u8(std::vector<std::byte>& out, std::uint8_t value) {
    out.push_back(static_cast<std::byte>(value));
}

void write_u16(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void write_u32(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 24) & 0xFF));
}

void write_u64(std::vector<std::byte>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFF));
    }
}

void write_bytes(std::vector<std::byte>& out, std::span<const std::byte> data) {
    out.insert(out.end(), data.begin(), data.end());
}

void write_string(std::vector<std::byte>& out, const std::string& str) {
    write_u32(out, static_cast<std::uint32_t>(str.size()));
    for (char c : str) {
        out.push_back(static_cast<std::byte>(c));
    }
}

std::uint8_t read_u8(std::span<const std::byte> data, std::size_t& offset) {
    if (offset >= data.size())
        return 0;
    return static_cast<std::uint8_t>(data[offset++]);
}

std::uint32_t read_u32(std::span<const std::byte> data, std::size_t& offset) {
    if (offset + 4 > data.size())
        return 0;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[offset + i])) << (i * 8);
    }
    offset += 4;
    return value;
}

std::uint64_t read_u64(std::span<const std::byte> data, std::size_t& offset) {
    if (offset + 8 > data.size())
        return 0;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data[offset + i])) << (i * 8);
    }
    offset += 8;
    return value;
}

std::vector<std::byte> read_bytes(std::span<const std::byte> data, std::size_t& offset,
                                  std::size_t count) {
    if (offset + count > data.size())
        return {};
    std::vector<std::byte> result(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                  data.begin() + static_cast<std::ptrdiff_t>(offset + count));
    offset += count;
    return result;
}

std::string read_string(std::span<const std::byte> data, std::size_t& offset) {
    std::uint32_t len = read_u32(data, offset);
    if (offset + len > data.size())
        return "";
    std::string result;
    result.reserve(len);
    for (std::uint32_t i = 0; i < len; ++i) {
        result.push_back(static_cast<char>(data[offset + i]));
    }
    offset += len;
    return result;
}

}  // namespace

// ============================================================================
// NodeId Implementation
// ============================================================================

std::string NodeId::to_hex() const {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

std::optional<NodeId> NodeId::from_hex(std::string_view hex) {
    if (hex.size() != 32)
        return std::nullopt;

    NodeId result;
    for (std::size_t i = 0; i < 16; ++i) {
        auto high = hex[i * 2];
        auto low = hex[i * 2 + 1];

        auto hex_digit = [](char c) -> std::optional<std::uint8_t> {
            if (c >= '0' && c <= '9')
                return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f')
                return static_cast<std::uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F')
                return static_cast<std::uint8_t>(c - 'A' + 10);
            return std::nullopt;
        };

        auto h = hex_digit(high);
        auto l = hex_digit(low);
        if (!h || !l)
            return std::nullopt;

        result.data[i] = static_cast<std::uint8_t>((*h << 4) | *l);
    }
    return result;
}

// ============================================================================
// CRC32 Calculation
// ============================================================================

std::uint32_t MessageSerializer::calculate_crc32(std::span<const std::byte> data) {
    std::uint32_t crc = 0xFFFFFFFF;
    for (auto b : data) {
        crc = CRC32_TABLE[(crc ^ static_cast<std::uint8_t>(b)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// Utility Serializers
// ============================================================================

void MessageSerializer::serialize_node_id(const NodeId& id, std::vector<std::byte>& out) {
    for (auto b : id.data) {
        out.push_back(static_cast<std::byte>(b));
    }
}

MessageSerializer::Result<NodeId>
MessageSerializer::deserialize_node_id(std::span<const std::byte> data) {
    if (data.size() < 16) {
        return ReplicationError::DeserializationFailed;
    }
    NodeId id;
    for (std::size_t i = 0; i < 16; ++i) {
        id.data[i] = static_cast<std::uint8_t>(data[i]);
    }
    return id;
}

void MessageSerializer::serialize_lsn(LSN lsn, std::vector<std::byte>& out) {
    write_u64(out, lsn.value);
}

MessageSerializer::Result<LSN> MessageSerializer::deserialize_lsn(std::span<const std::byte> data) {
    if (data.size() < 8) {
        return ReplicationError::DeserializationFailed;
    }
    std::size_t offset = 0;
    return LSN{read_u64(data, offset)};
}

void MessageSerializer::serialize_term(Term term, std::vector<std::byte>& out) {
    write_u64(out, term.value);
}

MessageSerializer::Result<Term>
MessageSerializer::deserialize_term(std::span<const std::byte> data) {
    if (data.size() < 8) {
        return ReplicationError::DeserializationFailed;
    }
    std::size_t offset = 0;
    return Term{read_u64(data, offset)};
}

void MessageSerializer::serialize_log_index(LogIndex index, std::vector<std::byte>& out) {
    write_u64(out, index.value);
}

MessageSerializer::Result<LogIndex>
MessageSerializer::deserialize_log_index(std::span<const std::byte> data) {
    if (data.size() < 8) {
        return ReplicationError::DeserializationFailed;
    }
    std::size_t offset = 0;
    return LogIndex{read_u64(data, offset)};
}

// ============================================================================
// Message Building Helper
// ============================================================================

std::vector<std::byte> MessageSerializer::build_message(MessageType type,
                                                        std::span<const std::byte> payload) {
    std::vector<std::byte> result;
    result.reserve(MESSAGE_HEADER_SIZE + payload.size());

    // Header
    write_u8(result, static_cast<std::uint8_t>(type));  // Byte 0: Message type
    write_u8(result, MESSAGE_FORMAT_VERSION);           // Byte 1: Version
    write_u16(result, 0);                               // Bytes 2-3: Reserved

    auto payload_len = static_cast<std::uint32_t>(payload.size());
    write_u32(result, payload_len);  // Bytes 4-7: Payload length

    auto checksum = calculate_crc32(payload);
    write_u32(result, checksum);  // Bytes 8-11: CRC32

    write_u32(result, 0);  // Bytes 12-15: Reserved

    // Payload
    write_bytes(result, payload);

    return result;
}

// ============================================================================
// Peek Functions
// ============================================================================

MessageSerializer::Result<MessageType>
MessageSerializer::peek_type(std::span<const std::byte> data) {
    if (data.size() < MESSAGE_HEADER_SIZE) {
        return ReplicationError::InvalidMessage;
    }
    return static_cast<MessageType>(static_cast<std::uint8_t>(data[0]));
}

MessageSerializer::Result<std::size_t>
MessageSerializer::peek_size(std::span<const std::byte> data) {
    if (data.size() < MESSAGE_HEADER_SIZE) {
        return ReplicationError::InvalidMessage;
    }
    std::size_t offset = 4;
    auto payload_len = read_u32(data, offset);
    return MESSAGE_HEADER_SIZE + payload_len;
}

// ============================================================================
// Serialization Implementations
// ============================================================================

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const RequestVote& msg) {
    std::vector<std::byte> payload;
    serialize_term(msg.term, payload);
    serialize_node_id(msg.candidate_id, payload);
    serialize_log_index(msg.last_log_index, payload);
    serialize_term(msg.last_log_term, payload);
    return build_message(MessageType::RequestVote, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const RequestVoteResponse& msg) {
    std::vector<std::byte> payload;
    serialize_term(msg.term, payload);
    write_u8(payload, msg.vote_granted ? 1 : 0);
    return build_message(MessageType::RequestVoteResponse, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const AppendEntries& msg) {
    std::vector<std::byte> payload;
    serialize_term(msg.term, payload);
    serialize_node_id(msg.leader_id, payload);
    serialize_log_index(msg.prev_log_index, payload);
    serialize_term(msg.prev_log_term, payload);
    serialize_log_index(msg.leader_commit, payload);

    // Entries count
    write_u32(payload, static_cast<std::uint32_t>(msg.entries.size()));

    // Serialize each entry
    for (const auto& entry : msg.entries) {
        serialize_term(entry.term, payload);
        serialize_log_index(entry.index, payload);
        write_u8(payload, static_cast<std::uint8_t>(entry.command.type));
        write_u32(payload, static_cast<std::uint32_t>(entry.command.data.size()));
        write_bytes(payload, entry.command.data);
    }

    return build_message(MessageType::AppendEntries, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const AppendEntriesResponse& msg) {
    std::vector<std::byte> payload;
    serialize_term(msg.term, payload);
    write_u8(payload, msg.success ? 1 : 0);
    serialize_log_index(msg.match_index, payload);
    return build_message(MessageType::AppendEntriesResponse, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const InstallSnapshot& msg) {
    std::vector<std::byte> payload;
    serialize_term(msg.term, payload);
    serialize_node_id(msg.leader_id, payload);
    serialize_log_index(msg.last_included_index, payload);
    serialize_term(msg.last_included_term, payload);
    write_u64(payload, msg.offset);
    write_u32(payload, static_cast<std::uint32_t>(msg.data.size()));
    write_bytes(payload, msg.data);
    write_u8(payload, msg.done ? 1 : 0);
    return build_message(MessageType::InstallSnapshot, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const InstallSnapshotResponse& msg) {
    std::vector<std::byte> payload;
    serialize_term(msg.term, payload);
    return build_message(MessageType::InstallSnapshotResponse, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const DeltaBatch& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.sender_id, payload);
    serialize_lsn(msg.start_lsn, payload);
    serialize_lsn(msg.end_lsn, payload);

    // Entry count
    write_u32(payload, static_cast<std::uint32_t>(msg.entries.size()));

    // Serialize each LogRecord
    for (const auto& record : msg.entries) {
        auto serialized = record.serialize();
        write_u32(payload, static_cast<std::uint32_t>(serialized.size()));
        write_bytes(payload, serialized);
    }

    return build_message(MessageType::DeltaBatch, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const DeltaAck& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.node_id, payload);
    serialize_lsn(msg.acked_lsn, payload);
    write_u8(payload, msg.success ? 1 : 0);
    write_string(payload, msg.error_msg);
    return build_message(MessageType::DeltaAck, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const DeltaRequest& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.node_id, payload);
    serialize_lsn(msg.start_lsn, payload);
    write_u32(payload, msg.max_entries);
    write_u32(payload, msg.max_bytes);
    return build_message(MessageType::DeltaRequest, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const SnapshotChunk& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.sender_id, payload);
    serialize_lsn(msg.snapshot_lsn, payload);
    write_u32(payload, msg.chunk_index);
    write_u32(payload, msg.total_chunks);
    write_u64(payload, msg.total_bytes);
    write_u32(payload, static_cast<std::uint32_t>(msg.data.size()));
    write_bytes(payload, msg.data);
    write_u32(payload, msg.checksum);
    write_u8(payload, msg.is_last ? 1 : 0);
    return build_message(MessageType::SnapshotChunk, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const SnapshotAck& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.node_id, payload);
    write_u32(payload, msg.chunk_index);
    write_u8(payload, msg.success ? 1 : 0);
    write_string(payload, msg.error_msg);
    return build_message(MessageType::SnapshotAck, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const SnapshotRequest& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.node_id, payload);
    serialize_lsn(msg.from_lsn, payload);
    return build_message(MessageType::SnapshotRequest, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const Heartbeat& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.sender_id, payload);
    write_u64(payload, msg.timestamp_us);
    serialize_lsn(msg.current_lsn, payload);
    write_u8(payload, msg.is_leader ? 1 : 0);
    return build_message(MessageType::Heartbeat, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const HeartbeatResponse& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.node_id, payload);
    write_u64(payload, msg.timestamp_us);
    serialize_lsn(msg.current_lsn, payload);
    return build_message(MessageType::HeartbeatResponse, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const ClusterConfig& msg) {
    std::vector<std::byte> payload;
    write_u64(payload, msg.config_index);
    write_u32(payload, static_cast<std::uint32_t>(msg.members.size()));
    for (const auto& member : msg.members) {
        serialize_node_id(member, payload);
    }
    write_u32(payload, static_cast<std::uint32_t>(msg.learners.size()));
    for (const auto& learner : msg.learners) {
        serialize_node_id(learner, payload);
    }
    return build_message(MessageType::ClusterConfig, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const ConfigChangeRequest& msg) {
    std::vector<std::byte> payload;
    serialize_node_id(msg.requester_id, payload);
    write_u8(payload, static_cast<std::uint8_t>(msg.change_type));
    serialize_node_id(msg.target_node, payload);
    write_string(payload, msg.address);
    return build_message(MessageType::ConfigChangeRequest, payload);
}

MessageSerializer::Result<std::vector<std::byte>>
MessageSerializer::serialize(const ReplicationMessage& msg) {
    return std::visit([](const auto& m) -> Result<std::vector<std::byte>> { return serialize(m); },
                      msg);
}

// ============================================================================
// Deserialization Implementations
// ============================================================================

MessageSerializer::Result<ReplicationMessage>
MessageSerializer::deserialize(std::span<const std::byte> data) {
    if (data.size() < MESSAGE_HEADER_SIZE) {
        return ReplicationError::InvalidMessage;
    }

    std::size_t offset = 0;
    auto msg_type = static_cast<MessageType>(read_u8(data, offset));
    auto version = read_u8(data, offset);

    if (version != MESSAGE_FORMAT_VERSION) {
        return ReplicationError::InvalidMessage;
    }

    offset += 2;  // Skip reserved bytes
    auto payload_len = read_u32(data, offset);
    auto expected_checksum = read_u32(data, offset);
    offset += 4;  // Skip reserved bytes

    if (data.size() < MESSAGE_HEADER_SIZE + payload_len) {
        return ReplicationError::DeserializationFailed;
    }

    auto payload = data.subspan(MESSAGE_HEADER_SIZE, payload_len);
    auto actual_checksum = calculate_crc32(payload);

    if (expected_checksum != actual_checksum) {
        return ReplicationError::ChecksumMismatch;
    }

    switch (msg_type) {
        case MessageType::RequestVote:
            return deserialize_request_vote(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::RequestVoteResponse:
            return deserialize_request_vote_response(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::AppendEntries:
            return deserialize_append_entries(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::AppendEntriesResponse:
            return deserialize_append_entries_response(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::InstallSnapshot:
            return deserialize_install_snapshot(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::InstallSnapshotResponse:
            return deserialize_install_snapshot_response(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::DeltaBatch:
            return deserialize_delta_batch(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::DeltaAck:
            return deserialize_delta_ack(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::DeltaRequest:
            return deserialize_delta_request(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::SnapshotChunk:
            return deserialize_snapshot_chunk(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::SnapshotAck:
            return deserialize_snapshot_ack(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::SnapshotRequest:
            return deserialize_snapshot_request(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::Heartbeat:
            return deserialize_heartbeat(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::HeartbeatResponse:
            return deserialize_heartbeat_response(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::ClusterConfig:
            return deserialize_cluster_config(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        case MessageType::ConfigChangeRequest:
            return deserialize_config_change_request(payload).map(
                [](auto&& m) -> ReplicationMessage { return m; });
        default:
            return ReplicationError::InvalidMessage;
    }
}

// ============================================================================
// Individual Deserializers
// ============================================================================

MessageSerializer::Result<RequestVote>
MessageSerializer::deserialize_request_vote(std::span<const std::byte> payload) {
    if (payload.size() < 8 + 16 + 8 + 8) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    RequestVote msg;
    msg.term = Term{read_u64(payload, offset)};
    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.candidate_id = id_result.value();
    offset += 16;
    msg.last_log_index = LogIndex{read_u64(payload, offset)};
    msg.last_log_term = Term{read_u64(payload, offset)};
    return msg;
}

MessageSerializer::Result<RequestVoteResponse>
MessageSerializer::deserialize_request_vote_response(std::span<const std::byte> payload) {
    if (payload.size() < 8 + 1) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    RequestVoteResponse msg;
    msg.term = Term{read_u64(payload, offset)};
    msg.vote_granted = read_u8(payload, offset) != 0;
    return msg;
}

MessageSerializer::Result<AppendEntries>
MessageSerializer::deserialize_append_entries(std::span<const std::byte> payload) {
    if (payload.size() < 8 + 16 + 8 + 8 + 8 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    AppendEntries msg;
    msg.term = Term{read_u64(payload, offset)};

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.leader_id = id_result.value();
    offset += 16;

    msg.prev_log_index = LogIndex{read_u64(payload, offset)};
    msg.prev_log_term = Term{read_u64(payload, offset)};
    msg.leader_commit = LogIndex{read_u64(payload, offset)};

    auto entry_count = read_u32(payload, offset);

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        if (offset + 8 + 8 + 1 + 4 > payload.size()) {
            return ReplicationError::DeserializationFailed;
        }

        RaftLogEntry entry;
        entry.term = Term{read_u64(payload, offset)};
        entry.index = LogIndex{read_u64(payload, offset)};
        entry.command.type = static_cast<RaftCommandType>(read_u8(payload, offset));
        auto data_len = read_u32(payload, offset);

        if (offset + data_len > payload.size()) {
            return ReplicationError::DeserializationFailed;
        }

        entry.command.data = read_bytes(payload, offset, data_len);
        msg.entries.push_back(std::move(entry));
    }

    return msg;
}

MessageSerializer::Result<AppendEntriesResponse>
MessageSerializer::deserialize_append_entries_response(std::span<const std::byte> payload) {
    if (payload.size() < 8 + 1 + 8) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    AppendEntriesResponse msg;
    msg.term = Term{read_u64(payload, offset)};
    msg.success = read_u8(payload, offset) != 0;
    msg.match_index = LogIndex{read_u64(payload, offset)};
    return msg;
}

MessageSerializer::Result<InstallSnapshot>
MessageSerializer::deserialize_install_snapshot(std::span<const std::byte> payload) {
    if (payload.size() < 8 + 16 + 8 + 8 + 8 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    InstallSnapshot msg;
    msg.term = Term{read_u64(payload, offset)};

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.leader_id = id_result.value();
    offset += 16;

    msg.last_included_index = LogIndex{read_u64(payload, offset)};
    msg.last_included_term = Term{read_u64(payload, offset)};
    msg.offset = read_u64(payload, offset);

    auto data_len = read_u32(payload, offset);
    if (offset + data_len + 1 > payload.size()) {
        return ReplicationError::DeserializationFailed;
    }

    msg.data = read_bytes(payload, offset, data_len);
    msg.done = read_u8(payload, offset) != 0;
    return msg;
}

MessageSerializer::Result<InstallSnapshotResponse>
MessageSerializer::deserialize_install_snapshot_response(std::span<const std::byte> payload) {
    if (payload.size() < 8) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    InstallSnapshotResponse msg;
    msg.term = Term{read_u64(payload, offset)};
    return msg;
}

MessageSerializer::Result<DeltaBatch>
MessageSerializer::deserialize_delta_batch(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8 + 8 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    DeltaBatch msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.sender_id = id_result.value();
    offset += 16;

    msg.start_lsn = LSN{read_u64(payload, offset)};
    msg.end_lsn = LSN{read_u64(payload, offset)};

    auto entry_count = read_u32(payload, offset);

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        if (offset + 4 > payload.size()) {
            return ReplicationError::DeserializationFailed;
        }

        auto record_len = read_u32(payload, offset);
        if (offset + record_len > payload.size()) {
            return ReplicationError::DeserializationFailed;
        }

        auto record_data = payload.subspan(offset, record_len);
        auto record_result = LogRecord::deserialize(record_data);
        if (record_result.is_err()) {
            return ReplicationError::DeserializationFailed;
        }

        msg.entries.push_back(std::move(record_result).value());
        offset += record_len;
    }

    return msg;
}

MessageSerializer::Result<DeltaAck>
MessageSerializer::deserialize_delta_ack(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8 + 1 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    DeltaAck msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.node_id = id_result.value();
    offset += 16;

    msg.acked_lsn = LSN{read_u64(payload, offset)};
    msg.success = read_u8(payload, offset) != 0;
    msg.error_msg = read_string(payload, offset);
    return msg;
}

MessageSerializer::Result<DeltaRequest>
MessageSerializer::deserialize_delta_request(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8 + 4 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    DeltaRequest msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.node_id = id_result.value();
    offset += 16;

    msg.start_lsn = LSN{read_u64(payload, offset)};
    msg.max_entries = read_u32(payload, offset);
    msg.max_bytes = read_u32(payload, offset);
    return msg;
}

MessageSerializer::Result<SnapshotChunk>
MessageSerializer::deserialize_snapshot_chunk(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8 + 4 + 4 + 8 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    SnapshotChunk msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.sender_id = id_result.value();
    offset += 16;

    msg.snapshot_lsn = LSN{read_u64(payload, offset)};
    msg.chunk_index = read_u32(payload, offset);
    msg.total_chunks = read_u32(payload, offset);
    msg.total_bytes = read_u64(payload, offset);

    auto data_len = read_u32(payload, offset);
    if (offset + data_len + 4 + 1 > payload.size()) {
        return ReplicationError::DeserializationFailed;
    }

    msg.data = read_bytes(payload, offset, data_len);
    msg.checksum = read_u32(payload, offset);
    msg.is_last = read_u8(payload, offset) != 0;
    return msg;
}

MessageSerializer::Result<SnapshotAck>
MessageSerializer::deserialize_snapshot_ack(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 4 + 1 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    SnapshotAck msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.node_id = id_result.value();
    offset += 16;

    msg.chunk_index = read_u32(payload, offset);
    msg.success = read_u8(payload, offset) != 0;
    msg.error_msg = read_string(payload, offset);
    return msg;
}

MessageSerializer::Result<SnapshotRequest>
MessageSerializer::deserialize_snapshot_request(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    SnapshotRequest msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.node_id = id_result.value();
    offset += 16;

    msg.from_lsn = LSN{read_u64(payload, offset)};
    return msg;
}

MessageSerializer::Result<Heartbeat>
MessageSerializer::deserialize_heartbeat(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8 + 8 + 1) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    Heartbeat msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.sender_id = id_result.value();
    offset += 16;

    msg.timestamp_us = read_u64(payload, offset);
    msg.current_lsn = LSN{read_u64(payload, offset)};
    msg.is_leader = read_u8(payload, offset) != 0;
    return msg;
}

MessageSerializer::Result<HeartbeatResponse>
MessageSerializer::deserialize_heartbeat_response(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 8 + 8) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    HeartbeatResponse msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.node_id = id_result.value();
    offset += 16;

    msg.timestamp_us = read_u64(payload, offset);
    msg.current_lsn = LSN{read_u64(payload, offset)};
    return msg;
}

MessageSerializer::Result<ClusterConfig>
MessageSerializer::deserialize_cluster_config(std::span<const std::byte> payload) {
    if (payload.size() < 8 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    ClusterConfig msg;

    msg.config_index = read_u64(payload, offset);

    auto member_count = read_u32(payload, offset);
    for (std::uint32_t i = 0; i < member_count; ++i) {
        if (offset + 16 > payload.size()) {
            return ReplicationError::DeserializationFailed;
        }
        auto id_result = deserialize_node_id(payload.subspan(offset, 16));
        if (id_result.is_err())
            return id_result.error();
        msg.members.push_back(id_result.value());
        offset += 16;
    }

    if (offset + 4 > payload.size()) {
        return ReplicationError::DeserializationFailed;
    }

    auto learner_count = read_u32(payload, offset);
    for (std::uint32_t i = 0; i < learner_count; ++i) {
        if (offset + 16 > payload.size()) {
            return ReplicationError::DeserializationFailed;
        }
        auto id_result = deserialize_node_id(payload.subspan(offset, 16));
        if (id_result.is_err())
            return id_result.error();
        msg.learners.push_back(id_result.value());
        offset += 16;
    }

    return msg;
}

MessageSerializer::Result<ConfigChangeRequest>
MessageSerializer::deserialize_config_change_request(std::span<const std::byte> payload) {
    if (payload.size() < 16 + 1 + 16 + 4) {
        return ReplicationError::DeserializationFailed;
    }

    std::size_t offset = 0;
    ConfigChangeRequest msg;

    auto id_result = deserialize_node_id(payload.subspan(offset, 16));
    if (id_result.is_err())
        return id_result.error();
    msg.requester_id = id_result.value();
    offset += 16;

    msg.change_type = static_cast<RaftCommandType>(read_u8(payload, offset));

    auto target_result = deserialize_node_id(payload.subspan(offset, 16));
    if (target_result.is_err())
        return target_result.error();
    msg.target_node = target_result.value();
    offset += 16;

    msg.address = read_string(payload, offset);
    return msg;
}

}  // namespace dotvm::core::state::replication
