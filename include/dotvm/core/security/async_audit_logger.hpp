#pragma once

/// @file async_audit_logger.hpp
/// @brief SEC-006 High-throughput async audit logger with lock-free ring buffer
///
/// Provides an asynchronous audit logger designed for minimal latency impact
/// on the hot path while supporting >10K events/sec throughput.

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "dotvm/core/security/audit_logger.hpp"

namespace dotvm::core::security::audit {

// ============================================================================
// Cache Line Constants
// ============================================================================

/// @brief Cache line size for alignment (common x86-64 value)
inline constexpr std::size_t AUDIT_CACHE_LINE_SIZE = 64;

// ============================================================================
// AsyncAuditLogger Class
// ============================================================================

/// @brief High-throughput async audit logger using lock-free ring buffer
///
/// Design:
/// - SPSC (Single-Producer Single-Consumer) ring buffer for lock-free writes
/// - Background consumer thread flushes events to persistent storage
/// - Minimal blocking on the hot path (log() call)
/// - Power-of-2 buffer size for efficient modulo via bitmask
///
/// Performance Target: >10K events/sec with <100us latency impact
///
/// Thread Safety:
/// - log() is thread-safe (but designed for single-producer pattern)
/// - query()/export_to() may acquire mutex during flush
///
/// Memory Layout:
/// - Producer index and consumer index are on separate cache lines
/// - Prevents false sharing between writer and reader
///
/// @par Usage Example
/// @code
/// auto logger = AsyncAuditLogger::create();
/// logger->start();  // Start background consumer
///
/// // In security context (hot path):
/// logger->log(AuditEvent::now(AuditEventType::AllocationAttempt));
///
/// // Before shutdown:
/// logger->stop();   // Graceful shutdown with flush
/// @endcode
class AsyncAuditLogger final : public AuditLogger {
public:
    /// @brief Construct logger with specified capacity
    ///
    /// @param capacity Ring buffer capacity (will be rounded up to power of 2)
    /// @param flush_interval_ms Flush interval in milliseconds (0 = continuous)
    explicit AsyncAuditLogger(std::size_t capacity = 4096,
                              std::uint32_t flush_interval_ms = 100) noexcept;

    ~AsyncAuditLogger() override;

    // Non-copyable, non-movable (has atomic members and background thread)
    AsyncAuditLogger(const AsyncAuditLogger&) = delete;
    AsyncAuditLogger& operator=(const AsyncAuditLogger&) = delete;
    AsyncAuditLogger(AsyncAuditLogger&&) = delete;
    AsyncAuditLogger& operator=(AsyncAuditLogger&&) = delete;

    // === AuditLogger Interface ===

    /// @brief Log an event (lock-free on hot path)
    ///
    /// This method is designed for minimal latency:
    /// - Lock-free write to ring buffer
    /// - No heap allocation
    /// - No blocking (may drop events if buffer full)
    void log(const AuditEvent& event) noexcept override;

    [[nodiscard]] bool is_enabled() const noexcept override { return true; }

    [[nodiscard]] std::vector<AuditEvent> query(const AuditQuery& filter) const override;

    std::size_t export_to(std::ostream& out, ExportFormat format,
                          const AuditQuery& filter) const override;

    void set_retention(std::uint32_t hours) noexcept override;

    [[nodiscard]] std::uint32_t retention() const noexcept override;

    [[nodiscard]] AuditLoggerStats stats() const noexcept override;

    void clear() noexcept override;

    // === Async Control ===

    /// @brief Start background consumer thread
    ///
    /// Must be called before logging events. Can be called multiple times
    /// (will no-op if already running).
    void start();

    /// @brief Stop background thread (waits for pending events to flush)
    ///
    /// Blocks until all pending events are processed and thread exits.
    void stop() noexcept;

    /// @brief Force immediate flush of pending events
    ///
    /// Wakes up consumer thread and waits for it to process all pending events.
    void flush() noexcept;

    /// @brief Check if background thread is running
    [[nodiscard]] bool is_running() const noexcept;

    // === Factory Methods ===

    /// @brief Create with default settings (4K capacity, 100ms flush)
    [[nodiscard]] static std::unique_ptr<AsyncAuditLogger> create();

    /// @brief Create for high-throughput scenarios (16K capacity, 50ms flush)
    [[nodiscard]] static std::unique_ptr<AsyncAuditLogger> high_throughput();

    /// @brief Create for low-latency scenarios (1K capacity, 10ms flush)
    [[nodiscard]] static std::unique_ptr<AsyncAuditLogger> low_latency();

    // === Configuration ===

    /// @brief Get ring buffer capacity
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// @brief Get flush interval
    [[nodiscard]] std::uint32_t flush_interval_ms() const noexcept { return flush_interval_ms_; }

private:
    // === Ring Buffer Entry ===

    struct RingBufferEntry {
        AuditEvent event{};
        std::atomic<bool> ready{false};  ///< Set by producer, cleared by consumer
    };

    // === Ring Buffer Storage ===

    std::unique_ptr<RingBufferEntry[]> buffer_;
    std::size_t capacity_;
    std::size_t capacity_mask_;  // For fast modulo (capacity - 1)

    // === Producer State (cache-line isolated) ===

    alignas(AUDIT_CACHE_LINE_SIZE) std::atomic<std::size_t> write_idx_{0};
    [[maybe_unused]] char pad_producer_[AUDIT_CACHE_LINE_SIZE - sizeof(std::atomic<std::size_t>)];

    // === Consumer State (cache-line isolated) ===

    alignas(AUDIT_CACHE_LINE_SIZE) std::atomic<std::size_t> read_idx_{0};
    [[maybe_unused]] char pad_consumer_[AUDIT_CACHE_LINE_SIZE - sizeof(std::atomic<std::size_t>)];

    // === Persistent Storage ===

    mutable std::mutex storage_mutex_;
    std::vector<AuditEvent> storage_;

    // === Background Thread ===

    std::thread consumer_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::condition_variable flush_cv_;
    mutable std::mutex flush_mutex_;
    std::atomic<bool> flush_requested_{false};

    // === Configuration ===

    std::uint32_t flush_interval_ms_;
    std::atomic<std::uint32_t> retention_hours_{0};

    // === Statistics ===

    std::atomic<std::size_t> events_logged_{0};
    std::atomic<std::size_t> events_dropped_{0};
    std::atomic<std::size_t> events_flushed_{0};
    std::atomic<std::size_t> events_expired_{0};

    // === Internal Methods ===

    /// @brief Background consumer loop
    void consumer_loop();

    /// @brief Process pending events in ring buffer
    /// @return Number of events processed
    std::size_t drain_buffer();

    /// @brief Apply retention policy to storage
    void apply_retention() noexcept;

    /// @brief Round up to next power of two
    [[nodiscard]] static std::size_t next_power_of_two(std::size_t n) noexcept;

    /// @brief Check if event matches query filter
    [[nodiscard]] static bool matches_filter(const AuditEvent& event,
                                             const AuditQuery& filter) noexcept;
};

// Static assertions for lock-free operation
static_assert(std::atomic<std::size_t>::is_always_lock_free,
              "AsyncAuditLogger requires lock-free size_t atomics");
static_assert(std::atomic<bool>::is_always_lock_free,
              "AsyncAuditLogger requires lock-free bool atomics");

}  // namespace dotvm::core::security::audit
