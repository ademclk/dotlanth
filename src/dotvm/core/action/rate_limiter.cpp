#include "dotvm/core/action/rate_limiter.hpp"

namespace dotvm::core::action {

ActionCallTracker::ActionCallTracker(std::string name, std::uint32_t max_calls) noexcept
    : action_name(std::move(name)), max_calls_per_minute(max_calls), last_seen(Clock::now()) {}

void ActionCallTracker::prune_locked(TimePoint now) noexcept {
    const auto cutoff = now - std::chrono::seconds(60);
    while (!call_timestamps.empty() && call_timestamps.front() < cutoff) {
        call_timestamps.pop_front();
    }
}

std::shared_ptr<ActionCallTracker>
ActionRateLimiter::get_or_create_tracker(const std::string& action_name,
                                         std::uint32_t max_calls_per_minute) noexcept {
    {
        std::shared_lock lock(trackers_mutex_);
        auto it = trackers_.find(action_name);
        if (it != trackers_.end()) {
            return it->second;
        }
    }

    std::unique_lock lock(trackers_mutex_);
    auto it = trackers_.find(action_name);
    if (it != trackers_.end()) {
        return it->second;
    }

    auto tracker = std::make_shared<ActionCallTracker>(action_name, max_calls_per_minute);
    trackers_[action_name] = tracker;
    return tracker;
}

bool ActionRateLimiter::try_acquire(const std::string& action_name,
                                    std::uint32_t max_calls_per_minute) noexcept {
    if (max_calls_per_minute == 0) {
        return true;  // No rate limit
    }

    auto tracker = get_or_create_tracker(action_name, max_calls_per_minute);
    const auto now = Clock::now();

    std::lock_guard lock(tracker->mutex);
    tracker->max_calls_per_minute = max_calls_per_minute;
    tracker->prune_locked(now);
    tracker->last_seen = now;

    if (tracker->call_timestamps.size() < max_calls_per_minute) {
        tracker->call_timestamps.push_back(now);
        return true;
    }

    return false;
}

void ActionRateLimiter::reset(const std::string& action_name) noexcept {
    std::shared_lock lock(trackers_mutex_);
    auto it = trackers_.find(action_name);
    if (it != trackers_.end()) {
        std::lock_guard tracker_lock(it->second->mutex);
        it->second->call_timestamps.clear();
        it->second->last_seen = Clock::now();
    }
}

std::uint32_t ActionRateLimiter::current_count(const std::string& action_name) noexcept {
    std::shared_lock lock(trackers_mutex_);
    auto it = trackers_.find(action_name);
    if (it == trackers_.end()) {
        return 0;
    }

    const auto now = Clock::now();
    std::lock_guard tracker_lock(it->second->mutex);
    it->second->prune_locked(now);
    return static_cast<std::uint32_t>(it->second->call_timestamps.size());
}

void ActionRateLimiter::cleanup_inactive(TimePoint now) noexcept {
    std::unique_lock lock(trackers_mutex_);
    if (now - last_cleanup_ < kCleanupInterval) {
        return;
    }
    last_cleanup_ = now;

    for (auto it = trackers_.begin(); it != trackers_.end();) {
        std::lock_guard tracker_lock(it->second->mutex);
        it->second->prune_locked(now);
        if (it->second->call_timestamps.empty() && (now - it->second->last_seen) > kInactiveTtl) {
            it = trackers_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace dotvm::core::action
