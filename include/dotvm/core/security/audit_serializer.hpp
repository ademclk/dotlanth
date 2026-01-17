#pragma once

/// @file audit_serializer.hpp
/// @brief SEC-006 Audit Event serialization utilities
///
/// Provides JSON, binary, and text serialization for audit events.
/// No external dependencies - uses hand-crafted serialization.

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

#include "dotvm/core/result.hpp"
#include "dotvm/core/security/audit_event.hpp"

namespace dotvm::core::security::audit {

// ============================================================================
// AuditSerializer Class
// ============================================================================

/// @brief Serializer for audit events
///
/// Provides JSON Lines and binary serialization for export functionality.
/// All methods are static - no instance state needed.
///
/// JSON Format (per event):
/// @code
/// {
///   "type": "DotStarted",
///   "severity": "Info",
///   "timestamp_ns": 1234567890,
///   "permission": "Execute",
///   "value": 0,
///   "dot_id": 42,
///   "capability_id": 0,
///   "message": "Starting execution",
///   "metadata": {"key": "value"}
/// }
/// @endcode
class AuditSerializer {
public:
    // === JSON Serialization ===

    /// @brief Serialize event to JSON string
    ///
    /// @param event The event to serialize
    /// @return JSON string representation
    [[nodiscard]] static std::string to_json(const AuditEvent& event);

    /// @brief Serialize multiple events to JSON Lines (one JSON object per
    /// line)
    ///
    /// @param events The events to serialize
    /// @param out The output stream
    static void to_json_lines(const std::vector<AuditEvent>& events, std::ostream& out);

    /// @brief Parse an event from JSON string
    ///
    /// @param json The JSON string
    /// @return Parsed event or error message
    [[nodiscard]] static Result<AuditEvent, std::string> from_json(std::string_view json);

    // === Binary Serialization ===

    /// @brief Serialize event to binary format
    ///
    /// Binary format (little-endian):
    /// - uint8_t: type
    /// - uint8_t: severity
    /// - uint64_t: timestamp_ns (nanoseconds since epoch)
    /// - uint32_t: permission
    /// - uint64_t: value
    /// - uint64_t: dot_id
    /// - uint64_t: capability_id
    /// - uint32_t: message_length
    /// - bytes: message (UTF-8)
    /// - uint32_t: metadata_count
    /// - for each metadata:
    ///   - uint32_t: key_length
    ///   - bytes: key
    ///   - uint32_t: value_length
    ///   - bytes: value
    ///
    /// @param event The event to serialize
    /// @return Binary data
    [[nodiscard]] static std::vector<std::uint8_t> to_binary(const AuditEvent& event);

    /// @brief Deserialize event from binary format
    ///
    /// @param data The binary data
    /// @return Parsed event or error message
    [[nodiscard]] static Result<AuditEvent, std::string>
    from_binary(std::span<const std::uint8_t> data);

    /// @brief Write binary events to stream
    ///
    /// @param events The events to serialize
    /// @param out The output stream
    static void to_binary_stream(const std::vector<AuditEvent>& events, std::ostream& out);

    // === Text Serialization ===

    /// @brief Serialize event to human-readable text
    ///
    /// Format: [TIMESTAMP] [SEVERITY] [TYPE] message {metadata}
    ///
    /// @param event The event to serialize
    /// @return Text representation
    [[nodiscard]] static std::string to_text(const AuditEvent& event);

    /// @brief Write text events to stream
    ///
    /// @param events The events to serialize
    /// @param out The output stream
    static void to_text_stream(const std::vector<AuditEvent>& events, std::ostream& out);

    // === Utility ===

    /// @brief Escape a string for JSON output
    ///
    /// @param str The string to escape
    /// @return Escaped string (without surrounding quotes)
    [[nodiscard]] static std::string escape_json_string(std::string_view str);

    /// @brief Format a timestamp for human-readable output
    ///
    /// @param tp The time point
    /// @return Formatted string (ISO 8601 style, relative to process start)
    [[nodiscard]] static std::string format_timestamp(std::chrono::steady_clock::time_point tp);

    /// @brief Get timestamp as nanoseconds since steady_clock epoch
    ///
    /// @param tp The time point
    /// @return Nanoseconds since epoch
    [[nodiscard]] static std::int64_t
    timestamp_to_ns(std::chrono::steady_clock::time_point tp) noexcept;

    /// @brief Convert nanoseconds to time point
    ///
    /// @param ns Nanoseconds since epoch
    /// @return Time point
    [[nodiscard]] static std::chrono::steady_clock::time_point
    ns_to_timestamp(std::int64_t ns) noexcept;
};

}  // namespace dotvm::core::security::audit
