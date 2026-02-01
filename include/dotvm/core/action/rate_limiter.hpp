#pragma once

/// @file rate_limiter.hpp
/// @brief DEP-005 Rate limiting for action invocations
///
/// Provides a sliding window rate limiter for action calls.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace dotvm::core::action {

// ============================================================================
// ActionCallTracker
// ============================================================================

/// @brief Tracks call timestamps for a single action
struct ActionCallTracker {
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    std::string action_name;
    std::deque<TimePoint> call_timestamps;
    std::uint32_t max_calls_per_minute{0};
    TimePoint last_seen{};
    mutable std::mutex mutex;

    explicit ActionCallTracker(std::string name, std::uint32_t max_calls) noexcept;

    ActionCallTracker(const ActionCallTracker&) = delete;
    ActionCallTracker& operator=(const ActionCallTracker&) = delete;
    ActionCallTracker(ActionCallTracker&&) = delete;
    ActionCallTracker& operator=(ActionCallTracker&&) = delete;

    void prune_locked(TimePoint now) noexcept;
};

// ============================================================================
// ActionRateLimiter
// ============================================================================

/// @brief Thread-safe rate limiter for action invocations
class ActionRateLimiter {
public:
    using Clock = ActionCallTracker::Clock;
    using TimePoint = ActionCallTracker::TimePoint;

    ActionRateLimiter() noexcept = default;
    ~ActionRateLimiter() = default;

    ActionRateLimiter(const ActionRateLimiter&) = delete;
    ActionRateLimiter& operator=(const ActionRateLimiter&) = delete;
    ActionRateLimiter(ActionRateLimiter&&) = delete;
    ActionRateLimiter& operator=(ActionRateLimiter&&) = delete;

    /// @brief Try to acquire a call slot for an action
    /// @return true if call is allowed, false if rate limited
    [[nodiscard]] bool try_acquire(const std::string& action_name,
                                   std::uint32_t max_calls_per_minute) noexcept;

    /// @brief Reset call history for an action
    void reset(const std::string& action_name) noexcept;

    /// @brief Get current call count for an action in the window
    [[nodiscard]] std::uint32_t current_count(const std::string& action_name) noexcept;

private:
    static constexpr auto kWindow = std::chrono::seconds(60);
    static constexpr auto kCleanupInterval = std::chrono::seconds(30);
    static constexpr auto kInactiveTtl = std::chrono::minutes(5);
    static constexpr std::uint64_t kCleanupEveryOps = 4096;

    std::unordered_map<std::string, std::shared_ptr<ActionCallTracker>> trackers_;
    mutable std::shared_mutex trackers_mutex_;
    std::atomic<std::uint64_t> op_counter_{0};
    TimePoint last_cleanup_{};

    std::shared_ptr<ActionCallTracker>
    get_or_create_tracker(const std::string& action_name,
                          std::uint32_t max_calls_per_minute) noexcept;
    void cleanup_inactive(TimePoint now) noexcept;
};

}  // namespace dotvm::core::action
