#pragma once

/// @file replication_error.hpp
/// @brief STATE-006 Replication-specific error codes
///
/// Error codes for multi-node state replication operations, covering
/// transport, consensus, and synchronization failures.

#include <cstdint>
#include <format>
#include <string_view>

namespace dotvm::core::state::replication {

// ============================================================================
// Replication Error Enum
// ============================================================================

/// @brief Error codes for replication operations
///
/// Error codes are grouped by category in the 144-175 range:
/// - 144-151: Transport errors (connection, stream, timeout)
/// - 152-159: Consensus errors (election, log, quorum)
/// - 160-167: Sync errors (delta, snapshot, verification)
/// - 168-175: Configuration errors (membership, cluster)
enum class ReplicationError : std::uint8_t {
    // Transport errors (144-151)
    ConnectionFailed = 144,      ///< Failed to establish connection
    ConnectionClosed = 145,      ///< Connection closed unexpectedly
    StreamError = 146,           ///< Stream read/write error
    Timeout = 147,               ///< Operation timed out
    TlsError = 148,              ///< TLS handshake or certificate error
    BackpressureExceeded = 149,  ///< Send buffer overflow

    // Consensus errors (152-159)
    NotLeader = 152,            ///< Node is not the leader
    NoLeader = 153,             ///< No leader elected
    ElectionTimeout = 154,      ///< Leader election timed out
    LogInconsistent = 155,      ///< Log entries inconsistent
    QuorumNotReached = 156,     ///< Failed to reach majority
    TermMismatch = 157,         ///< Raft term mismatch
    InvalidLogIndex = 158,      ///< Log index out of range
    StaleRead = 159,            ///< Read from stale replica
    AlreadyVoted = 150,         ///< Already voted in this term
    LogIndexGap = 151,          ///< Non-contiguous log index
    LogTruncated = 175,         ///< Log entry was truncated

    // Sync errors (160-167)
    DeltaApplyFailed = 160,     ///< Failed to apply delta batch
    SnapshotTransferFailed = 161,  ///< Snapshot transfer interrupted
    VerificationFailed = 162,   ///< MPT root hash mismatch
    LsnGap = 163,               ///< LSN sequence gap detected
    ChecksumMismatch = 164,     ///< Data checksum verification failed
    InvalidMessage = 165,       ///< Malformed replication message
    DeserializationFailed = 166,  ///< Failed to deserialize message
    SerializationFailed = 167,  ///< Failed to serialize message

    // Configuration errors (168-175)
    InvalidNodeId = 168,        ///< Node ID is invalid
    NodeNotFound = 169,         ///< Node not in cluster membership
    NodeAlreadyExists = 170,    ///< Node already in cluster
    ClusterNotInitialized = 171,  ///< Cluster not yet bootstrapped
    ConfigurationInProgress = 172,  ///< Membership change already pending
    InvalidClusterConfig = 173,  ///< Invalid cluster configuration
    ShuttingDown = 174,         ///< Node is shutting down
};

/// @brief Convert replication error to human-readable string
[[nodiscard]] constexpr std::string_view to_string(ReplicationError error) noexcept {
    switch (error) {
        // Transport errors
        case ReplicationError::ConnectionFailed:
            return "ConnectionFailed";
        case ReplicationError::ConnectionClosed:
            return "ConnectionClosed";
        case ReplicationError::StreamError:
            return "StreamError";
        case ReplicationError::Timeout:
            return "Timeout";
        case ReplicationError::TlsError:
            return "TlsError";
        case ReplicationError::BackpressureExceeded:
            return "BackpressureExceeded";

        // Consensus errors
        case ReplicationError::NotLeader:
            return "NotLeader";
        case ReplicationError::NoLeader:
            return "NoLeader";
        case ReplicationError::ElectionTimeout:
            return "ElectionTimeout";
        case ReplicationError::LogInconsistent:
            return "LogInconsistent";
        case ReplicationError::QuorumNotReached:
            return "QuorumNotReached";
        case ReplicationError::TermMismatch:
            return "TermMismatch";
        case ReplicationError::InvalidLogIndex:
            return "InvalidLogIndex";
        case ReplicationError::StaleRead:
            return "StaleRead";
        case ReplicationError::AlreadyVoted:
            return "AlreadyVoted";
        case ReplicationError::LogIndexGap:
            return "LogIndexGap";
        case ReplicationError::LogTruncated:
            return "LogTruncated";

        // Sync errors
        case ReplicationError::DeltaApplyFailed:
            return "DeltaApplyFailed";
        case ReplicationError::SnapshotTransferFailed:
            return "SnapshotTransferFailed";
        case ReplicationError::VerificationFailed:
            return "VerificationFailed";
        case ReplicationError::LsnGap:
            return "LsnGap";
        case ReplicationError::ChecksumMismatch:
            return "ChecksumMismatch";
        case ReplicationError::InvalidMessage:
            return "InvalidMessage";
        case ReplicationError::DeserializationFailed:
            return "DeserializationFailed";
        case ReplicationError::SerializationFailed:
            return "SerializationFailed";

        // Configuration errors
        case ReplicationError::InvalidNodeId:
            return "InvalidNodeId";
        case ReplicationError::NodeNotFound:
            return "NodeNotFound";
        case ReplicationError::NodeAlreadyExists:
            return "NodeAlreadyExists";
        case ReplicationError::ClusterNotInitialized:
            return "ClusterNotInitialized";
        case ReplicationError::ConfigurationInProgress:
            return "ConfigurationInProgress";
        case ReplicationError::InvalidClusterConfig:
            return "InvalidClusterConfig";
        case ReplicationError::ShuttingDown:
            return "ShuttingDown";
    }
    return "Unknown";
}

/// @brief Check if a replication error is recoverable
///
/// Recoverable errors indicate the operation can be retried or the system
/// can continue operating (possibly after retry or failover).
///
/// @param error The error to check
/// @return true if the error is recoverable
[[nodiscard]] constexpr bool is_recoverable(ReplicationError error) noexcept {
    switch (error) {
        // Transport errors - can retry with reconnection
        case ReplicationError::Timeout:
        case ReplicationError::BackpressureExceeded:
            return true;

        // Consensus errors - can retry or wait for new leader
        case ReplicationError::NotLeader:
        case ReplicationError::NoLeader:
        case ReplicationError::ElectionTimeout:
        case ReplicationError::QuorumNotReached:
        case ReplicationError::StaleRead:
            return true;

        // Sync errors - can request retransmission
        case ReplicationError::LsnGap:
            return true;

        // Configuration errors - can wait and retry
        case ReplicationError::ConfigurationInProgress:
            return true;

        default:
            return false;
    }
}

/// @brief Check if an error is a transport-level error
[[nodiscard]] constexpr bool is_transport_error(ReplicationError error) noexcept {
    auto code = static_cast<std::uint8_t>(error);
    return code >= 144 && code <= 151;
}

/// @brief Check if an error is a consensus-level error
[[nodiscard]] constexpr bool is_consensus_error(ReplicationError error) noexcept {
    auto code = static_cast<std::uint8_t>(error);
    return code >= 152 && code <= 159;
}

/// @brief Check if an error is a sync-level error
[[nodiscard]] constexpr bool is_sync_error(ReplicationError error) noexcept {
    auto code = static_cast<std::uint8_t>(error);
    return code >= 160 && code <= 167;
}

/// @brief Check if an error is a configuration-level error
[[nodiscard]] constexpr bool is_config_error(ReplicationError error) noexcept {
    auto code = static_cast<std::uint8_t>(error);
    return code >= 168 && code <= 175;
}

}  // namespace dotvm::core::state::replication

// ============================================================================
// std::formatter specialization for ReplicationError
// ============================================================================

template <>
struct std::formatter<dotvm::core::state::replication::ReplicationError>
    : std::formatter<std::string_view> {
    auto format(dotvm::core::state::replication::ReplicationError e,
                std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
