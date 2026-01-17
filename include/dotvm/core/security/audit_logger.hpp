#pragma once

/// @file audit_logger.hpp
/// @brief SEC-006 Audit Logger interface and basic implementations
///
/// This header defines the enhanced AuditLogger interface with support for
/// querying, exporting, and retention policies. It also provides basic
/// implementations including NullAuditLogger and SimpleBufferedAuditLogger.
///
/// @note Types are defined in the dotvm::core::security::audit namespace
/// to avoid collision with legacy types in security_context.hpp.

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <vector>

#include "dotvm/core/security/audit_event.hpp"

namespace dotvm::core::security::audit {

// ============================================================================
// ExportFormat Enum
// ============================================================================

/// @brief Export format for audit logs
enum class ExportFormat : std::uint8_t {
    Json = 0,    ///< JSON Lines format (one JSON object per line)
    Binary = 1,  ///< Compact binary format for efficient storage
    Text = 2     ///< Human-readable text format
};

/// @brief Convert ExportFormat to string
[[nodiscard]] constexpr const char* to_string(ExportFormat format) noexcept {
    switch (format) {
        case ExportFormat::Json:
            return "Json";
        case ExportFormat::Binary:
            return "Binary";
        case ExportFormat::Text:
            return "Text";
    }
    return "Unknown";
}

// ============================================================================
// AuditQuery Struct
// ============================================================================

/// @brief Query filter for retrieving audit events
///
/// All filter fields are optional. When not set, they don't filter.
/// Multiple filters are combined with AND logic.
struct AuditQuery {
    /// Filter by specific event type (nullopt = all types)
    std::optional<AuditEventType> type = std::nullopt;

    /// Filter by minimum severity (nullopt = all severities)
    std::optional<AuditSeverity> min_severity = std::nullopt;

    /// Filter by Dot ID (nullopt = all Dots)
    std::optional<DotId> dot_id = std::nullopt;

    /// Filter by capability ID (nullopt = all capabilities)
    std::optional<CapabilityId> capability_id = std::nullopt;

    /// Filter events after this timestamp (default = beginning of time)
    std::chrono::steady_clock::time_point since = {};

    /// Filter events before this timestamp (default = end of time)
    std::chrono::steady_clock::time_point until{std::chrono::steady_clock::time_point::max()};

    /// Maximum number of events to return
    std::size_t limit{100};

    /// @brief Create a query that matches all events
    [[nodiscard]] static AuditQuery all(std::size_t lim = 1000) noexcept {
        return AuditQuery{.limit = lim};
    }

    /// @brief Create a query for a specific event type
    [[nodiscard]] static AuditQuery by_type(AuditEventType t, std::size_t lim = 100) noexcept {
        return AuditQuery{.type = t, .limit = lim};
    }

    /// @brief Create a query for events at or above a severity level
    [[nodiscard]] static AuditQuery by_severity(AuditSeverity min_sev,
                                                std::size_t lim = 100) noexcept {
        return AuditQuery{.min_severity = min_sev, .limit = lim};
    }

    /// @brief Create a query for a specific Dot
    [[nodiscard]] static AuditQuery by_dot(DotId dot, std::size_t lim = 100) noexcept {
        return AuditQuery{.dot_id = dot, .limit = lim};
    }

    /// @brief Create a query for events since a timestamp
    [[nodiscard]] static AuditQuery since_time(std::chrono::steady_clock::time_point tp,
                                               std::size_t lim = 100) noexcept {
        return AuditQuery{.since = tp, .limit = lim};
    }
};

// ============================================================================
// AuditLoggerStats Struct
// ============================================================================

/// @brief Statistics about audit logger operation
struct AuditLoggerStats {
    /// Total events logged since creation
    std::size_t events_logged{0};

    /// Events dropped due to buffer overflow
    std::size_t events_dropped{0};

    /// Current buffer capacity
    std::size_t buffer_capacity{0};

    /// Current buffer utilization
    std::size_t buffer_used{0};

    /// Events exported via export_to()
    std::size_t events_exported{0};

    /// Events removed due to retention policy
    std::size_t events_expired{0};
};

// ============================================================================
// AuditLogger Interface
// ============================================================================

/// @brief Abstract interface for security audit logging (SEC-006 enhanced)
///
/// Implementations can store events in memory, write to files, or forward
/// to external systems. The interface is designed for minimal overhead
/// when logging is disabled.
///
/// Thread Safety: Depends on implementation. See specific logger classes.
///
/// @par Hot Path Considerations
/// The log() method is designed for use in performance-critical paths.
/// It is marked noexcept and implementations should minimize blocking.
class AuditLogger {
public:
    virtual ~AuditLogger() = default;

    // Non-copyable, non-movable (interface class)
    AuditLogger(const AuditLogger&) = delete;
    AuditLogger& operator=(const AuditLogger&) = delete;
    AuditLogger(AuditLogger&&) = delete;
    AuditLogger& operator=(AuditLogger&&) = delete;

protected:
    AuditLogger() = default;

    // === Core Logging ===

    /// @brief Log a security event
    ///
    /// @param event The event to log
    /// @note Must be noexcept for use in security-critical paths.
    ///       Implementations may drop events if buffer is full.
    virtual void log(const AuditEvent& event) noexcept = 0;

    /// @brief Check if logging is enabled
    ///
    /// @return true if log() will actually record events
    [[nodiscard]] virtual bool is_enabled() const noexcept = 0;

    // === Query Interface (SEC-006) ===

    /// @brief Query stored events matching criteria
    ///
    /// @param filter The query filter criteria
    /// @return Vector of matching events, sorted by timestamp (oldest first)
    [[nodiscard]] virtual std::vector<AuditEvent> query(const AuditQuery& filter) const = 0;

    /// @brief Get the count of events matching criteria (without retrieving)
    ///
    /// @param filter The query filter criteria
    /// @return Number of matching events
    [[nodiscard]] virtual std::size_t count(const AuditQuery& filter) const {
        return query(filter).size();  // Default implementation
    }

    // === Export Interface (SEC-006) ===

    /// @brief Export events to an output stream
    ///
    /// @param out The output stream to write to
    /// @param format The export format (JSON, Binary, Text)
    /// @param filter Optional filter criteria (default = all events)
    /// @return Number of events exported
    virtual std::size_t export_to(std::ostream& out, ExportFormat format = ExportFormat::Json,
                                  const AuditQuery& filter = {}) const = 0;

    // === Retention Policy (SEC-006) ===

    /// @brief Set retention policy (hours, 0 = unlimited)
    ///
    /// Events older than the retention period may be automatically removed.
    /// The exact timing of removal is implementation-defined.
    ///
    /// @param hours Number of hours to retain events (0 = keep forever)
    virtual void set_retention(std::uint32_t hours) noexcept = 0;

    /// @brief Get current retention policy (hours)
    ///
    /// @return Retention period in hours (0 = unlimited)
    [[nodiscard]] virtual std::uint32_t retention() const noexcept = 0;

    // === Statistics ===

    /// @brief Get logger statistics
    ///
    /// @return Current statistics snapshot
    [[nodiscard]] virtual AuditLoggerStats stats() const noexcept = 0;

    // === Utility ===

    /// @brief Clear all stored events
    virtual void clear() noexcept = 0;
};

// ============================================================================
// NullAuditLogger
// ============================================================================

/// @brief No-op audit logger (default when logging disabled)
///
/// Use this when audit logging is not needed. All operations are no-ops
/// with minimal overhead. Singleton pattern for efficiency.
class NullAuditLogger final : public AuditLogger {
public:
    void log(const AuditEvent& /*event*/) noexcept override {}

    [[nodiscard]] bool is_enabled() const noexcept override { return false; }

    [[nodiscard]] std::vector<AuditEvent> query(const AuditQuery& /*filter*/) const override {
        return {};
    }

    [[nodiscard]] std::size_t count(const AuditQuery& /*filter*/) const override { return 0; }

    std::size_t export_to(std::ostream& /*out*/, ExportFormat /*format*/,
                          const AuditQuery& /*filter*/) const override {
        return 0;
    }

    void set_retention(std::uint32_t /*hours*/) noexcept override {}

    [[nodiscard]] std::uint32_t retention() const noexcept override { return 0; }

    [[nodiscard]] AuditLoggerStats stats() const noexcept override { return {}; }

    void clear() noexcept override {}

    /// @brief Get singleton instance
    [[nodiscard]] static NullAuditLogger& instance() noexcept {
        static NullAuditLogger instance;
        return instance;
    }
};

// ============================================================================
// SimpleBufferedAuditLogger
// ============================================================================

/// @brief Simple in-memory buffered audit logger
///
/// Stores events in a bounded buffer. When the buffer is full, oldest
/// events are overwritten (ring buffer behavior).
///
/// Thread Safety: NOT thread-safe. Use one per Dot (single-threaded entities).
/// For thread-safe logging, use AsyncAuditLogger.
///
/// @par Usage
/// @code
/// SimpleBufferedAuditLogger logger(1000);  // 1000 event capacity
/// logger.log(AuditEvent::now(AuditEventType::DotStarted));
/// auto events = logger.query(AuditQuery::all());
/// @endcode
class SimpleBufferedAuditLogger final : public AuditLogger {
public:
    /// @brief Construct with specified capacity
    ///
    /// @param capacity Maximum number of events to store (minimum 1)
    explicit SimpleBufferedAuditLogger(std::size_t capacity = 1024) noexcept;

    void log(const AuditEvent& event) noexcept override;

    [[nodiscard]] bool is_enabled() const noexcept override { return true; }

    [[nodiscard]] std::vector<AuditEvent> query(const AuditQuery& filter) const override;

    std::size_t export_to(std::ostream& out, ExportFormat format,
                          const AuditQuery& filter) const override;

    void set_retention(std::uint32_t hours) noexcept override { retention_hours_ = hours; }

    [[nodiscard]] std::uint32_t retention() const noexcept override { return retention_hours_; }

    [[nodiscard]] AuditLoggerStats stats() const noexcept override;

    void clear() noexcept override;

    // === Additional accessors ===

    /// @brief Get all logged events (in order)
    [[nodiscard]] std::vector<AuditEvent> events() const;

    /// @brief Get number of events stored
    [[nodiscard]] std::size_t size() const noexcept;

    /// @brief Get buffer capacity
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    std::vector<AuditEvent> events_;
    std::size_t capacity_;
    std::size_t write_index_{0};
    bool wrapped_{false};
    std::uint32_t retention_hours_{0};

    // Statistics
    std::size_t total_logged_{0};
    std::size_t total_dropped_{0};
    std::size_t total_exported_{0};
    std::size_t total_expired_{0};

    /// @brief Check if an event matches the query filter
    [[nodiscard]] bool matches_filter(const AuditEvent& event,
                                      const AuditQuery& filter) const noexcept;

    /// @brief Apply retention policy, removing old events
    void apply_retention() noexcept;
};

}  // namespace dotvm::core::security::audit

// Re-export key types in main namespace for convenience
namespace dotvm::core::security {
using audit::AuditLoggerStats;
using audit::AuditQuery;
using audit::ExportFormat;
}  // namespace dotvm::core::security
