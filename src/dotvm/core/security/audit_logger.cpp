/// @file audit_logger.cpp
/// @brief SEC-006 Audit Logger implementations

#include "dotvm/core/security/audit_logger.hpp"

#include <algorithm>
#include <ostream>

#include "dotvm/core/security/audit_serializer.hpp"

namespace dotvm::core::security::audit {

// ============================================================================
// SimpleBufferedAuditLogger Implementation
// ============================================================================

SimpleBufferedAuditLogger::SimpleBufferedAuditLogger(std::size_t capacity) noexcept
    : capacity_(capacity > 0 ? capacity : 1) {
    events_.reserve(capacity_);
}

void SimpleBufferedAuditLogger::log(const AuditEvent& event) noexcept {
    ++total_logged_;

    // Apply retention policy periodically (every 1000 events)
    if ((total_logged_ % 1000) == 0 && retention_hours_ > 0) {
        apply_retention();
    }

    if (events_.size() < capacity_) {
        events_.push_back(event);
    } else {
        // Ring buffer: overwrite oldest
        events_[write_index_] = event;
        write_index_ = (write_index_ + 1) % capacity_;
        wrapped_ = true;
    }
}

bool SimpleBufferedAuditLogger::matches_filter(const AuditEvent& event,
                                               const AuditQuery& filter) const noexcept {
    // Type filter
    if (filter.type.has_value() && event.type != *filter.type) {
        return false;
    }

    // Severity filter (minimum level)
    if (filter.min_severity.has_value() && static_cast<std::uint8_t>(event.severity) <
                                               static_cast<std::uint8_t>(*filter.min_severity)) {
        return false;
    }

    // Dot ID filter
    if (filter.dot_id.has_value() && event.dot_id != *filter.dot_id) {
        return false;
    }

    // Capability ID filter
    if (filter.capability_id.has_value() && event.capability_id != *filter.capability_id) {
        return false;
    }

    // Time range filter
    if (event.timestamp < filter.since || event.timestamp > filter.until) {
        return false;
    }

    return true;
}

std::vector<AuditEvent> SimpleBufferedAuditLogger::query(const AuditQuery& filter) const {
    std::vector<AuditEvent> result;
    result.reserve(std::min(filter.limit, events_.size()));

    // Iterate in chronological order
    auto all_events = events();

    for (const auto& event : all_events) {
        if (result.size() >= filter.limit) {
            break;
        }
        if (matches_filter(event, filter)) {
            result.push_back(event);
        }
    }

    return result;
}

std::size_t SimpleBufferedAuditLogger::export_to(std::ostream& out, ExportFormat format,
                                                 const AuditQuery& filter) const {
    auto events_to_export = query(filter);

    switch (format) {
        case ExportFormat::Json:
            AuditSerializer::to_json_lines(events_to_export, out);
            break;
        case ExportFormat::Binary:
            AuditSerializer::to_binary_stream(events_to_export, out);
            break;
        case ExportFormat::Text:
            AuditSerializer::to_text_stream(events_to_export, out);
            break;
    }

    // Note: const_cast to update stats - stats are logically mutable
    const_cast<SimpleBufferedAuditLogger*>(this)->total_exported_ += events_to_export.size();

    return events_to_export.size();
}

AuditLoggerStats SimpleBufferedAuditLogger::stats() const noexcept {
    return AuditLoggerStats{
        .events_logged = total_logged_,
        .events_dropped = total_dropped_,
        .buffer_capacity = capacity_,
        .buffer_used = events_.size(),
        .events_exported = total_exported_,
        .events_expired = total_expired_,
    };
}

void SimpleBufferedAuditLogger::clear() noexcept {
    events_.clear();
    write_index_ = 0;
    wrapped_ = false;
}

std::vector<AuditEvent> SimpleBufferedAuditLogger::events() const {
    if (!wrapped_) {
        // Simple case: events are in order
        return events_;
    }

    // Ring buffer has wrapped - need to reorder
    std::vector<AuditEvent> result;
    result.reserve(events_.size());

    // First, add events from write_index_ to end (oldest)
    for (std::size_t i = write_index_; i < events_.size(); ++i) {
        result.push_back(events_[i]);
    }
    // Then, add events from start to write_index_ (newest)
    for (std::size_t i = 0; i < write_index_; ++i) {
        result.push_back(events_[i]);
    }

    return result;
}

std::size_t SimpleBufferedAuditLogger::size() const noexcept {
    return events_.size();
}

void SimpleBufferedAuditLogger::apply_retention() noexcept {
    if (retention_hours_ == 0 || events_.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::hours(static_cast<std::int64_t>(retention_hours_));

    // Remove events older than cutoff
    std::size_t removed = 0;
    auto new_end =
        std::remove_if(events_.begin(), events_.end(), [cutoff, &removed](const AuditEvent& e) {
            if (e.timestamp < cutoff) {
                ++removed;
                return true;
            }
            return false;
        });

    events_.erase(new_end, events_.end());
    total_expired_ += removed;

    // Reset ring buffer state after removal
    if (removed > 0) {
        write_index_ = 0;
        wrapped_ = false;
    }
}

}  // namespace dotvm::core::security::audit
