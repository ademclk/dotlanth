#pragma once

/// @file link_error.hpp
/// @brief DEP-004 Link-specific error codes
///
/// Error codes for relationship model operations, covering link existence,
/// cardinality, traversal, and cascade/storage operations.

#include <cstdint>
#include <format>
#include <string_view>

namespace dotvm::core::link {

// ============================================================================
// LinkError Enum
// ============================================================================

/// @brief Error codes for link operations
///
/// Error codes are grouped by category in the 192-207 range:
/// - 192-195: Link existence errors
/// - 196-199: Cardinality errors
/// - 200-203: Traversal errors
/// - 204-207: Cascade/Storage errors
enum class LinkError : std::uint8_t {
    // Link existence (192-195)
    LinkInstanceNotFound = 192,       ///< Link instance does not exist
    LinkInstanceAlreadyExists = 193,  ///< Link instance already exists
    InvalidObjectId = 194,            ///< ObjectId is invalid
    ObjectNotFound = 195,             ///< Object does not exist

    // Cardinality (196-199)
    CardinalityViolation = 196,    ///< Link cardinality constraint violated
    RequiredLinkMissing = 197,     ///< Required link is missing
    InverseLinkMismatch = 198,     ///< Inverse link consistency check failed
    LinkDefinitionNotFound = 199,  ///< Link definition does not exist

    // Traversal (200-203)
    TraversalPathEmpty = 200,      ///< Traversal path is empty
    TraversalPathInvalid = 201,    ///< Traversal path is invalid
    TraversalDepthExceeded = 202,  ///< Traversal depth exceeded
    TraversalCycleDetected = 203,  ///< Traversal encountered a cycle

    // Cascade/Storage (204-207)
    CascadeDeleteFailed = 204,  ///< Cascade delete operation failed
    TransactionRequired = 205,  ///< Operation requires an active transaction
    StorageError = 206,         ///< Storage backend error
    IndexCorruption = 207,      ///< Index corruption detected
};

/// @brief Convert LinkError to human-readable string
[[nodiscard]] constexpr std::string_view to_string(LinkError error) noexcept {
    switch (error) {
        case LinkError::LinkInstanceNotFound:
            return "LinkInstanceNotFound";
        case LinkError::LinkInstanceAlreadyExists:
            return "LinkInstanceAlreadyExists";
        case LinkError::InvalidObjectId:
            return "InvalidObjectId";
        case LinkError::ObjectNotFound:
            return "ObjectNotFound";
        case LinkError::CardinalityViolation:
            return "CardinalityViolation";
        case LinkError::RequiredLinkMissing:
            return "RequiredLinkMissing";
        case LinkError::InverseLinkMismatch:
            return "InverseLinkMismatch";
        case LinkError::LinkDefinitionNotFound:
            return "LinkDefinitionNotFound";
        case LinkError::TraversalPathEmpty:
            return "TraversalPathEmpty";
        case LinkError::TraversalPathInvalid:
            return "TraversalPathInvalid";
        case LinkError::TraversalDepthExceeded:
            return "TraversalDepthExceeded";
        case LinkError::TraversalCycleDetected:
            return "TraversalCycleDetected";
        case LinkError::CascadeDeleteFailed:
            return "CascadeDeleteFailed";
        case LinkError::TransactionRequired:
            return "TransactionRequired";
        case LinkError::StorageError:
            return "StorageError";
        case LinkError::IndexCorruption:
            return "IndexCorruption";
    }
    return "Unknown";
}

/// @brief Check if a link error is recoverable (can be retried or handled gracefully)
[[nodiscard]] constexpr bool is_recoverable(LinkError error) noexcept {
    switch (error) {
        case LinkError::LinkInstanceNotFound:
        case LinkError::LinkInstanceAlreadyExists:
        case LinkError::TraversalCycleDetected:
            return true;
        default:
            return false;
    }
}

}  // namespace dotvm::core::link

// ============================================================================
// std::formatter specialization for LinkError
// ============================================================================

template <>
struct std::formatter<dotvm::core::link::LinkError> : std::formatter<std::string_view> {
    auto format(dotvm::core::link::LinkError e, std::format_context& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
