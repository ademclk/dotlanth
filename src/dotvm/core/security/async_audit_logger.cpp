/// @file async_audit_logger.cpp
/// @brief SEC-006 Async audit logger implementation

#include "dotvm/core/security/async_audit_logger.hpp"

#include <algorithm>
#include <ostream>

#include "dotvm/core/security/audit_serializer.hpp"

namespace dotvm::core::security::audit {

// ============================================================================
// Construction / Destruction
// ============================================================================

AsyncAuditLogger::AsyncAuditLogger(std::size_t capacity, std::uint32_t flush_interval_ms) noexcept
    : capacity_(next_power_of_two(capacity)),
      capacity_mask_(capacity_ - 1),
      flush_interval_ms_(flush_interval_ms) {
    // Allocate ring buffer
    buffer_ = std::make_unique<RingBufferEntry[]>(capacity_);

    // Reserve some storage space
    storage_.reserve(capacity_);
}

AsyncAuditLogger::~AsyncAuditLogger() {
    stop();
}

// ============================================================================
// Core Logging (Hot Path)
// ============================================================================

void AsyncAuditLogger::log(const AuditEvent& event) noexcept {
    // Acquire write slot (atomic increment with relaxed ordering)
    auto idx = write_idx_.fetch_add(1, std::memory_order_relaxed);
    auto slot = idx & capacity_mask_;

    // Check if slot is available (consumer hasn't caught up)
    auto& entry = buffer_[slot];

    // Wait for slot to be consumed (spin with relaxed check)
    // In practice, this rarely blocks if buffer is sized correctly
    if (entry.ready.load(std::memory_order_acquire)) {
        // Buffer is full - drop event
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Write event data
    entry.event = event;

    // Mark slot as ready for consumer (release ensures event data is visible)
    entry.ready.store(true, std::memory_order_release);

    events_logged_.fetch_add(1, std::memory_order_relaxed);
}

// ============================================================================
// Query Interface
// ============================================================================

bool AsyncAuditLogger::matches_filter(const AuditEvent& event, const AuditQuery& filter) noexcept {
    if (filter.type.has_value() && event.type != *filter.type) {
        return false;
    }
    if (filter.min_severity.has_value() && static_cast<std::uint8_t>(event.severity) <
                                               static_cast<std::uint8_t>(*filter.min_severity)) {
        return false;
    }
    if (filter.dot_id.has_value() && event.dot_id != *filter.dot_id) {
        return false;
    }
    if (filter.capability_id.has_value() && event.capability_id != *filter.capability_id) {
        return false;
    }
    if (event.timestamp < filter.since || event.timestamp > filter.until) {
        return false;
    }
    return true;
}

std::vector<AuditEvent> AsyncAuditLogger::query(const AuditQuery& filter) const {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    std::vector<AuditEvent> result;
    result.reserve(std::min(filter.limit, storage_.size()));

    for (const auto& event : storage_) {
        if (result.size() >= filter.limit) {
            break;
        }
        if (matches_filter(event, filter)) {
            result.push_back(event);
        }
    }

    return result;
}

std::size_t AsyncAuditLogger::export_to(std::ostream& out, ExportFormat format,
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

    return events_to_export.size();
}

// ============================================================================
// Retention Policy
// ============================================================================

void AsyncAuditLogger::set_retention(std::uint32_t hours) noexcept {
    retention_hours_.store(hours, std::memory_order_relaxed);
}

std::uint32_t AsyncAuditLogger::retention() const noexcept {
    return retention_hours_.load(std::memory_order_relaxed);
}

void AsyncAuditLogger::apply_retention() noexcept {
    auto hours = retention_hours_.load(std::memory_order_relaxed);
    if (hours == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(storage_mutex_);

    if (storage_.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::hours(static_cast<std::int64_t>(hours));

    std::size_t removed = 0;
    auto new_end =
        std::remove_if(storage_.begin(), storage_.end(), [cutoff, &removed](const AuditEvent& e) {
            if (e.timestamp < cutoff) {
                ++removed;
                return true;
            }
            return false;
        });

    storage_.erase(new_end, storage_.end());
    events_expired_.fetch_add(removed, std::memory_order_relaxed);
}

// ============================================================================
// Statistics
// ============================================================================

AuditLoggerStats AsyncAuditLogger::stats() const noexcept {
    std::lock_guard<std::mutex> lock(storage_mutex_);

    return AuditLoggerStats{
        .events_logged = events_logged_.load(std::memory_order_relaxed),
        .events_dropped = events_dropped_.load(std::memory_order_relaxed),
        .buffer_capacity = capacity_,
        .buffer_used = storage_.size(),
        .events_exported = 0,  // Not tracked
        .events_expired = events_expired_.load(std::memory_order_relaxed),
    };
}

void AsyncAuditLogger::clear() noexcept {
    std::lock_guard<std::mutex> lock(storage_mutex_);
    storage_.clear();

    // Reset ring buffer entries
    for (std::size_t i = 0; i < capacity_; ++i) {
        buffer_[i].ready.store(false, std::memory_order_relaxed);
    }

    // Reset indices
    write_idx_.store(0, std::memory_order_relaxed);
    read_idx_.store(0, std::memory_order_relaxed);
}

// ============================================================================
// Async Control
// ============================================================================

void AsyncAuditLogger::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return;  // Already running
    }

    stop_requested_.store(false, std::memory_order_relaxed);
    consumer_thread_ = std::thread([this] { consumer_loop(); });
}

void AsyncAuditLogger::stop() noexcept {
    if (!running_.load(std::memory_order_acquire)) {
        return;  // Not running
    }

    // Signal stop
    stop_requested_.store(true, std::memory_order_release);

    // Wake up consumer thread
    {
        std::lock_guard<std::mutex> lock(flush_mutex_);
        flush_requested_.store(true, std::memory_order_release);
    }
    flush_cv_.notify_one();

    // Wait for thread to finish
    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }

    running_.store(false, std::memory_order_release);
}

void AsyncAuditLogger::flush() noexcept {
    if (!running_.load(std::memory_order_acquire)) {
        // Not running - flush synchronously
        drain_buffer();
        return;
    }

    // Request flush
    {
        std::lock_guard<std::mutex> lock(flush_mutex_);
        flush_requested_.store(true, std::memory_order_release);
    }
    flush_cv_.notify_one();

    // Wait for flush to complete by checking that buffer is drained
    // (Simple busy-wait with yield)
    while (flush_requested_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

bool AsyncAuditLogger::is_running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

// ============================================================================
// Background Consumer
// ============================================================================

void AsyncAuditLogger::consumer_loop() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        // Process any pending events
        drain_buffer();

        // Apply retention policy periodically
        apply_retention();

        // Clear flush flag after processing
        flush_requested_.store(false, std::memory_order_release);

        // Wait for next flush interval or explicit flush request
        {
            std::unique_lock<std::mutex> lock(flush_mutex_);
            flush_cv_.wait_for(lock, std::chrono::milliseconds(flush_interval_ms_), [this] {
                return flush_requested_.load(std::memory_order_acquire) ||
                       stop_requested_.load(std::memory_order_acquire);
            });
        }
    }

    // Final drain on stop
    drain_buffer();
    flush_requested_.store(false, std::memory_order_release);
}

std::size_t AsyncAuditLogger::drain_buffer() {
    std::size_t processed = 0;

    while (true) {
        auto read = read_idx_.load(std::memory_order_relaxed);
        auto slot = read & capacity_mask_;
        auto& entry = buffer_[slot];

        // Check if entry is ready
        if (!entry.ready.load(std::memory_order_acquire)) {
            break;  // No more ready entries
        }

        // Move event to storage
        {
            std::lock_guard<std::mutex> lock(storage_mutex_);
            storage_.push_back(std::move(entry.event));
        }

        // Mark slot as consumed
        entry.ready.store(false, std::memory_order_release);

        // Advance read index
        read_idx_.fetch_add(1, std::memory_order_relaxed);
        ++processed;
    }

    events_flushed_.fetch_add(processed, std::memory_order_relaxed);
    return processed;
}

// ============================================================================
// Factory Methods
// ============================================================================

std::unique_ptr<AsyncAuditLogger> AsyncAuditLogger::create() {
    return std::make_unique<AsyncAuditLogger>(4096, 100);
}

std::unique_ptr<AsyncAuditLogger> AsyncAuditLogger::high_throughput() {
    return std::make_unique<AsyncAuditLogger>(16384, 50);
}

std::unique_ptr<AsyncAuditLogger> AsyncAuditLogger::low_latency() {
    return std::make_unique<AsyncAuditLogger>(1024, 10);
}

// ============================================================================
// Utility
// ============================================================================

std::size_t AsyncAuditLogger::next_power_of_two(std::size_t n) noexcept {
    if (n == 0) {
        return 1;
    }

    // Handle already power of 2
    if ((n & (n - 1)) == 0) {
        return n;
    }

    // Find next power of 2
    std::size_t power = 1;
    while (power < n) {
        power <<= 1;
    }
    return power;
}

}  // namespace dotvm::core::security::audit
