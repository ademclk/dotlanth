/// @file audit_serializer.cpp
/// @brief SEC-006 Audit Event serialization implementation

#include "dotvm/core/security/audit_serializer.hpp"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace dotvm::core::security::audit {

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

/// @brief Write a little-endian value to a byte vector
template <typename T>
void write_le(std::vector<std::uint8_t>& out, T value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(bytes[i]);
    }
}

/// @brief Read a little-endian value from a byte span
template <typename T>
[[nodiscard]] bool
read_le(std::span<const std::uint8_t>& data, T& out) noexcept {
    if (data.size() < sizeof(T)) {
        return false;
    }
    std::memcpy(&out, data.data(), sizeof(T));
    data = data.subspan(sizeof(T));
    return true;
}

/// @brief Write a string with length prefix
void write_string(std::vector<std::uint8_t>& out, std::string_view str) {
    write_le(out, static_cast<std::uint32_t>(str.size()));
    for (char c : str) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
}

/// @brief Read a length-prefixed string
[[nodiscard]] bool read_string(std::span<const std::uint8_t>& data,
                               std::string& out) {
    std::uint32_t len = 0;
    if (!read_le(data, len)) {
        return false;
    }
    if (data.size() < len) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(data.data()), len);
    data = data.subspan(len);
    return true;
}

}  // namespace

// ============================================================================
// JSON Serialization
// ============================================================================

std::string AuditSerializer::escape_json_string(std::string_view str) {
    std::string result;
    result.reserve(str.size() + 16);

    for (char c : str) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control character - use unicode escape
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }

    return result;
}

std::string
AuditSerializer::format_timestamp(std::chrono::steady_clock::time_point tp) {
    auto ns = timestamp_to_ns(tp);
    auto secs = ns / 1'000'000'000;
    auto frac = ns % 1'000'000'000;

    std::ostringstream oss;
    oss << secs << "." << std::setw(9) << std::setfill('0') << frac;
    return oss.str();
}

std::int64_t AuditSerializer::timestamp_to_ns(
    std::chrono::steady_clock::time_point tp) noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               tp.time_since_epoch())
        .count();
}

std::chrono::steady_clock::time_point
AuditSerializer::ns_to_timestamp(std::int64_t ns) noexcept {
    return std::chrono::steady_clock::time_point(std::chrono::nanoseconds(ns));
}

std::string AuditSerializer::to_json(const AuditEvent& event) {
    std::ostringstream oss;

    oss << "{";
    oss << "\"type\":\"" << to_string(event.type) << "\",";
    oss << "\"severity\":\"" << to_string(event.severity) << "\",";
    oss << "\"timestamp_ns\":" << timestamp_to_ns(event.timestamp) << ",";
    oss << "\"permission\":\"" << to_string(event.permission) << "\",";
    oss << "\"value\":" << event.value << ",";
    oss << "\"dot_id\":" << event.dot_id << ",";
    oss << "\"capability_id\":" << event.capability_id << ",";
    oss << "\"message\":\"" << escape_json_string(event.message) << "\"";

    if (!event.metadata.empty()) {
        oss << ",\"metadata\":{";
        bool first = true;
        for (const auto& [key, value] : event.metadata) {
            if (!first) {
                oss << ",";
            }
            first = false;
            oss << "\"" << escape_json_string(key) << "\":\""
                << escape_json_string(value) << "\"";
        }
        oss << "}";
    }

    oss << "}";
    return oss.str();
}

void AuditSerializer::to_json_lines(const std::vector<AuditEvent>& events,
                                    std::ostream& out) {
    for (const auto& event : events) {
        out << to_json(event) << "\n";
    }
}

Result<AuditEvent, std::string>
AuditSerializer::from_json(std::string_view /*json*/) {
    // Basic JSON parsing - simplified implementation
    // For production, consider using a proper JSON library
    return Result<AuditEvent, std::string>{Err, std::string{"JSON parsing not implemented"}};
}

// ============================================================================
// Binary Serialization
// ============================================================================

std::vector<std::uint8_t> AuditSerializer::to_binary(const AuditEvent& event) {
    std::vector<std::uint8_t> result;
    result.reserve(128);  // Typical event size

    // Fixed fields
    write_le(result, static_cast<std::uint8_t>(event.type));
    write_le(result, static_cast<std::uint8_t>(event.severity));
    write_le(result, timestamp_to_ns(event.timestamp));
    write_le(result, static_cast<std::uint32_t>(event.permission));
    write_le(result, event.value);
    write_le(result, event.dot_id);
    write_le(result, event.capability_id);

    // Variable-length fields
    write_string(result, event.message);

    // Metadata
    write_le(result, static_cast<std::uint32_t>(event.metadata.size()));
    for (const auto& [key, value] : event.metadata) {
        write_string(result, key);
        write_string(result, value);
    }

    return result;
}

Result<AuditEvent, std::string>
AuditSerializer::from_binary(std::span<const std::uint8_t> data) {
    AuditEvent event;

    // Fixed fields
    std::uint8_t type_raw = 0;
    std::uint8_t severity_raw = 0;
    std::int64_t timestamp_ns = 0;
    std::uint32_t permission_raw = 0;

    if (!read_le(data, type_raw) || !read_le(data, severity_raw) ||
        !read_le(data, timestamp_ns) || !read_le(data, permission_raw) ||
        !read_le(data, event.value) || !read_le(data, event.dot_id) ||
        !read_le(data, event.capability_id)) {
        return Result<AuditEvent, std::string>{Err, std::string{"Failed to read fixed fields"}};
    }

    event.type = static_cast<AuditEventType>(type_raw);
    event.severity = static_cast<AuditSeverity>(severity_raw);
    event.timestamp = ns_to_timestamp(timestamp_ns);
    event.permission = static_cast<Permission>(permission_raw);

    // Message
    if (!read_string(data, event.message)) {
        return Result<AuditEvent, std::string>{Err, std::string{"Failed to read message"}};
    }

    // Metadata
    std::uint32_t metadata_count = 0;
    if (!read_le(data, metadata_count)) {
        return Result<AuditEvent, std::string>{Err, std::string{"Failed to read metadata count"}};
    }

    event.metadata.reserve(metadata_count);
    for (std::uint32_t i = 0; i < metadata_count; ++i) {
        std::string key;
        std::string value;
        if (!read_string(data, key) || !read_string(data, value)) {
            return Result<AuditEvent, std::string>{Err, std::string{"Failed to read metadata pair"}};
        }
        event.metadata.emplace_back(std::move(key), std::move(value));
    }

    return Result<AuditEvent, std::string>{Ok, std::move(event)};
}

void AuditSerializer::to_binary_stream(const std::vector<AuditEvent>& events,
                                       std::ostream& out) {
    // Write count first
    auto count = static_cast<std::uint32_t>(events.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    // Write each event
    for (const auto& event : events) {
        auto binary = to_binary(event);
        auto size = static_cast<std::uint32_t>(binary.size());
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(reinterpret_cast<const char*>(binary.data()),
                  static_cast<std::streamsize>(binary.size()));
    }
}

// ============================================================================
// Text Serialization
// ============================================================================

std::string AuditSerializer::to_text(const AuditEvent& event) {
    std::ostringstream oss;

    // Format: [TIMESTAMP] [SEVERITY] [TYPE] message {metadata}
    oss << "[" << format_timestamp(event.timestamp) << "] ";
    oss << "[" << to_string(event.severity) << "] ";
    oss << "[" << to_string(event.type) << "] ";

    if (event.dot_id != 0) {
        oss << "dot=" << event.dot_id << " ";
    }
    if (event.capability_id != 0) {
        oss << "cap=" << event.capability_id << " ";
    }
    if (event.permission != Permission::None) {
        oss << "perm=" << to_string(event.permission) << " ";
    }
    if (event.value != 0) {
        oss << "value=" << event.value << " ";
    }

    if (!event.message.empty()) {
        oss << "\"" << event.message << "\"";
    }

    if (!event.metadata.empty()) {
        oss << " {";
        bool first = true;
        for (const auto& [key, value] : event.metadata) {
            if (!first) {
                oss << ", ";
            }
            first = false;
            oss << key << "=" << value;
        }
        oss << "}";
    }

    return oss.str();
}

void AuditSerializer::to_text_stream(const std::vector<AuditEvent>& events,
                                     std::ostream& out) {
    for (const auto& event : events) {
        out << to_text(event) << "\n";
    }
}

}  // namespace dotvm::core::security::audit
